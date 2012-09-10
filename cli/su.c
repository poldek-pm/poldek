/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"

static void do_putenv(const char *var, const char *val)
{
#ifdef HAVE_SETENV        
        setenv(var, val, 1);
#else
        {
            int len = strlen(var) + strlen(val) + 3;
            char *tmp = n_malloc(len);
            n_snprintf(tmp, len, "%s=%s", var, val);
            putenv(tmp);
        }
#endif
}

/*
  Stolen from su for GNU.
  Copyright (C) 1992-2004 Free Software Foundation, Inc.
  License: GPL v2 or later.
*/
static
void modify_environment(const struct passwd *pw)
{
  char *term;
  char *display;
  char *path;
  
  term = getenv("TERM");
  display = getenv("DISPLAY");
  path = getenv("PATH");
  
#if HAVE_CLEARENV
  clearenv();
#else
  extern char **environ;   
  environ = n_malloc(2 * sizeof (char *));
  environ[0] = 0;
#endif
  
  if (term)
      do_putenv("TERM", term);
  
  if (display)
      do_putenv("DISPLAY", display);

  if (path)
      do_putenv("PATH", path);
  
  do_putenv("HOME", pw->pw_dir);
  do_putenv("SHELL", pw->pw_shell);
  do_putenv("USER", pw->pw_name);
  do_putenv("LOGNAME", pw->pw_name);
}


int poldek_su(const char *user) 
{
    struct passwd pw, *pwptr;
    char pwbuf[1024];
    
    if (getpwnam_r(user, &pw, pwbuf, sizeof(pwbuf), &pwptr) != 0) {
        logn(LOGERR, _("%s: could not retrieve account (%m)"), user);
        return 0;
    }
    
    /* Make sure pw_shell is non-NULL.  It may be NULL when NEW_USER
       is a username that is retrieved via NIS (YP), but that doesn't have
       a default shell listed.  */
    if (pw.pw_shell == NULL || pw.pw_shell[0] == '\0')
        pw.pw_shell = "/bin/sh";

    modify_environment(&pw);

    if (setgid(pw.pw_gid) != 0) {
        logn(LOGERR, _("setgid %s: %m"), user);
        return 0;
    }

    if (setuid (pw.pw_uid) != 0) {
        logn(LOGERR, _("setuid %s: %m"), user);
        return 0;
    }
    
    if (chdir(pw.pw_dir) != 0) {
        logn(LOGERR, _("chdir %s: %m"), pw.pw_dir);
        return 0;
    }

    msgn(2, _("Running as user '%s'\n"), user); 
    return 1;
}

    
