/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/nassert.h>
#include "i18n.h"
#include "log.h"
#include "term.h"
#include "pkg.h"


int ask_yn(int default_a, const char *fmt, ...) 
{
    va_list args;
    int a;

    
    if (!isatty(STDIN_FILENO))
        return default_a;               /* yes */

    va_start(args, fmt);
    vlog(LOGINFO, 0, fmt, args);
    va_end(args);
    
    a = askuser(STDIN_FILENO, "YyNn\n", NULL);
    a = toupper(a);
    switch(a) {
        case 'Y': a = 1; break;
        case 'N': a = 0; break;
        case '\n': a = default_a; break;
        default:
            n_assert(0);
    }
    msg(-1, "_\n");
    return a;
}


int ask_pkg(const char *capname, struct pkg **pkgs)
{
    int i, a;
    char *validchrs, *p;

    
    if (!isatty(STDIN_FILENO))
        return 0;
    
    msgn(-1, _("There are more than one package which provide \"%s\":"), capname);
    validchrs = alloca(64);
    p = validchrs;
    *p++ = '\n';

    i = 0;
    while(pkgs[i] != NULL && i < 24) {
        msgn(-1, "%c) %s", 'a' + i, pkg_snprintf_s(pkgs[i]));
        *p++ = 'a' + i;
        i++;
    }
    
    msg(-1, _("Which one do you want to install? [a]")); 
    a = askuser(STDIN_FILENO, validchrs, NULL);
    msg(-1, "_\n");
    
    if (a == '\n')
        return 0;
    
    a -= 'a';
    //printf("Selected %d\n", a);
    if (a >= 0 && a < i)
        return a;
    return 0;
}
