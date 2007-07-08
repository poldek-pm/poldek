/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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
#include "poldek_ts.h"
#include "poldek_intern.h"
#include "pkg.h"

#define msg_ask(fmt, args...) poldek_log(LOGTTY|LOGINFO, fmt, ## args)

static int term_confirm(void *foo, const struct poldek_ts *ts, int hint,
                        const char *question) 
{
    const char *yn = "[Y/n]";
    int a;

    foo = foo;
    ts = ts;
    
    if (!isatty(STDIN_FILENO))
        return hint;

    if (hint == 0)    /* no */
        yn = "[N/y]";

    msg_ask("%s %s", question, yn);
    
    a = poldek_term_ask(STDIN_FILENO, "YyNn\n", NULL);
    a = toupper(a);
    switch(a) {
        case 'Y': a = 1; break;
        case 'N': a = 0; break;
        case '\n': a = hint; break;
        default:
            n_assert(0);
    }
    
    msg_ask("_\n");
    return a;
}

static int term_ts_confirm(void *foo, const struct poldek_ts *ts)
{
    int answer = 1;
    foo = foo;
    
    /* poldek__ts_display_summary(ts); */ /* already displayed */
    
    if (ts->type == POLDEK_TS_UNINSTALL) {
        if (ts->getop(ts, POLDEK_OP_CONFIRM_UNINST))
            answer = term_confirm(foo, ts, 0, _("Proceed?"));
        
    } else {
        if (ts->getop(ts, POLDEK_OP_CONFIRM_INST))
            answer = term_confirm(foo, ts, 1, _("Proceed?"));
        
    }
    
    return answer;
}

static int term_choose_pkg(void *foo, const struct poldek_ts *ts,
                           const char *capname, tn_array *pkgs, int hint)
{
    char *validchrs, *p;
    int i, a;
    
    foo = foo; ts = ts;
    
    if (hint > 24)     /* over ascii */
        hint = 0;
    
    if (!isatty(STDIN_FILENO))
        return hint;
    
    msg_ask(_("There are more than one package which provide \"%s\":"), capname);
    msg_ask("_\n");
    validchrs = alloca(64);
    p = validchrs;
    *p++ = '\n';

    i = 0;
    for (i=0; i < n_array_size(pkgs); i++) {
        msgn(-1, "%c) %s", 'a' + i, pkg_id(n_array_nth(pkgs, i)));
        *p++ = 'a' + i;

        if (i > 24)
            break;
    }
    *p++ = 'Q';
    
    msg_ask(_("Which one do you want to install ('Q' to abort)? [%c]"), 'a' + hint);
    
    a = poldek_term_ask(STDIN_FILENO, validchrs, NULL);
    msg_ask("_\n");
    
    if (a == '\n')
        return hint;
    
    if (a == 'Q')
        return -1;
    
    a -= 'a';
    //printf("Selected %d\n", a);
    if (a >= 0 && a < i)
        return a;
    
    return hint;
}

int poldek__confirm(const struct poldek_ts *ts, int hint, const char *message)
{
    if (ts->ctx->confirm_fn == NULL)
        return hint;

    return ts->ctx->confirm_fn(ts->ctx->data_confirm_fn, ts, hint, message);
}

int poldek__ts_confirm(const struct poldek_ts *ts)
{
    if (ts->ctx->ts_confirm_fn == NULL)
        return 1;

    return ts->ctx->ts_confirm_fn(ts->ctx->data_ts_confirm_fn, ts);
}

int poldek__choose_equiv(const struct poldek_ts *ts,
                         const char *capname, tn_array *pkgs, struct pkg *hint)
{
    int i, inthint = 0;
    
    if (hint) {
        for (i=0; i < n_array_size(pkgs); i++) {
            if (hint && hint == n_array_nth(pkgs, i)) {
                inthint = i;
                break;
            }
        }
    }

    if (ts->ctx->choose_equiv_fn == NULL)
        return inthint;
    
    return ts->ctx->choose_equiv_fn(ts->ctx->data_choose_equiv_fn,
                                    ts, capname, pkgs, inthint);
}

void poldek__setup_default_ask_callbacks(struct poldek_ctx *ctx)
{
    ctx->data_confirm_fn = NULL;
    ctx->confirm_fn = term_confirm;

    ctx->data_ts_confirm_fn = NULL;
    ctx->ts_confirm_fn = term_ts_confirm;

    ctx->data_choose_equiv_fn = NULL;
    ctx->choose_equiv_fn = term_choose_pkg;
}
