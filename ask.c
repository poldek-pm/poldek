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
#include <unistd.h>

#include <trurl/nassert.h>

#include "i18n.h"
#include "log.h"
#include "poldek_term.h"
#include "pkg.h"


int poldek_term_ask_yn(int default_a, const char *fmt, ...) 
{
    va_list args;
    int a;

    
    if (!isatty(STDIN_FILENO))
        return default_a;               /* yes */

    va_start(args, fmt);
    vlog(LOGINFO, 0, fmt, args);
    va_end(args);
    
    a = poldek_term_ask(STDIN_FILENO, "YyNn\n", NULL);
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


int poldek_term_ask_pkg(const char *capname, struct pkg **pkgs, struct pkg *deflt)
{
    int i, a, default_i = 0;
    char *validchrs, *p;


    if (deflt) {
        i = 0;
        while(pkgs[i] != NULL && i < 24) {
            if (deflt && deflt == pkgs[i]) {
                default_i = i;
                break;
            }
            i++;
        }
    }

    
    if (!isatty(STDIN_FILENO))
        return default_i;
    
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
    
    msg(-1, _("Which one do you want to install? [%c]"), 'a' + default_i); 
    a = poldek_term_ask(STDIN_FILENO, validchrs, NULL);
    msg(-1, "_\n");
    
    if (a == '\n')
        return default_i;
    
    a -= 'a';
    //printf("Selected %d\n", a);
    if (a >= 0 && a < i)
        return a;
    
    return default_i;
}
