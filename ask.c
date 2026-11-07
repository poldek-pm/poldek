/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "poldek.h"
#include "poldek_term.h"
#include "poldek_ts.h"
#include "poldek_intern.h"
#include "pkg.h"
#include "capreq.h"

#define msg_ask(fmt, args...) poldek_log(LOGTTY|LOGINFO, fmt, ## args)

/* Asks prompt and waits for one of keys, given in lower case and taken in
   either; Enter gives defkey. Echoes the answer and returns it in lower case. */
static int ask(const char *prompt, const char *keys, int defkey)
{
    char valid[32];
    int i, n = 0, a;

    for (i = 0; keys[i] && n < (int)sizeof(valid) - 3; i++) {
        valid[n++] = keys[i];
        valid[n++] = toupper(keys[i]);
    }
    valid[n++] = '\n';
    valid[n] = '\0';

    poldek_term__ask_begin(STDIN_FILENO);
    msg_ask("%s", prompt);

    do {
        if ((a = poldek_term__getkey()) == -1)
            a = '\n';          /* the terminal is gone: the default answer */
    } while (a == '\0' || strchr(valid, a) == NULL);
    poldek_term__ask_end();

    a = (a == '\n') ? defkey : tolower(a);

    msg_ask("_ %c\n", a);
    return a;
}

static int term_confirm(void *foo, const struct poldek_ts *ts, int hint,
                        const char *question)
{
    char prompt[4096];

    foo = foo;
    ts = ts;

    if (!isatty(STDIN_FILENO))
        return hint;

    n_snprintf(prompt, sizeof(prompt), "%s %s", question,
               hint ? "[Y/n/q]" : "[N/y/q]");

    switch (ask(prompt, "ynq", hint ? 'y' : 'n')) {
        case 'y': return 1;
        case 'n': return 0;
        default:  return POLDEK_ANSWER_ABORT;
    }
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

static int term_choose_equiv(void *foo, const struct poldek_ts *ts,
                             const struct pkg *pkg, const char *capname,
                             tn_array *candidates, int hint)
{
    char *validchrs, *p;
    int i, j, a, lines;
    /* q and Q abort */
    char choice[] = "abcdefghijklmnoprstuvwxyz1234567890ABCDEFGHIJKLMNOPRSTUVWXYZ";

    foo = foo; ts = ts;
    j = 0, lines = 0;

    lines = poldek_term_get_height();

    if (lines <= 0)
        lines = 30;

    lines -= 6; // to show some info above

    if (lines < 6)
        lines = 6;

    if (lines > (int)sizeof(choice) - 1)
        lines = sizeof(choice) - 1;

    if (hint >= lines)     /* over ascii */
        j = (hint/lines)*lines; // first show page with hint

    if (!isatty(STDIN_FILENO))
        return hint;

    poldek_term__ask_begin(STDIN_FILENO);

    if (pkg) {
        msg_ask(_("%s: required \"%s\" is provided by the following packages:"),
                pkg_id(pkg), capname);
    } else {
        msg_ask(_("Required \"%s\" is provided by the following packages:"),
                capname);
    }

    msg_ask("_\n");

    validchrs = alloca(100);


onemoretime:
    memset(validchrs, 0, 100);
    p = validchrs;
    *p++ = '\n';

    if (j > 0) {
	msgn(-1, _("-/backspace/pgup) page up"));
	*p++ = '-';
	*p++ = 0x7f; // backspace
    }
    for (i = 0; i < lines && i+j < n_array_size(candidates); i++) {
        msgn(-1, "%c) %s", choice[i], pkg_id(n_array_nth(candidates, i+j)));
        *p++ = choice[i];
    }
    if (i+j < n_array_size(candidates)) {
	msgn(-1, _("+/space/tab/pgdown) page down"));
	*p++ = '+';
	*p++ = ' ';  // space
	*p++ = '\t'; // tab
    }

    *p++ = 'q';
    *p++ = 'Q';

    msg_ask(_("Which one do you want to install ('q' to abort)? [%s]"),
            pkg_id(n_array_nth(candidates, hint)));

    do {
        if ((a = poldek_term__getkey()) == -1)
            a = '\n';          /* the terminal is gone: the default answer */
    } while (a == '\0' || strchr(validchrs, a) == NULL);
    msg_ask("_\n");

    if (a == '-' || a == 0x7f) {
	if (j >= lines)
            j -= lines;
        goto onemoretime;
    }

    if (a == '+' || a == ' ' || a == '\t' ) {
	if (j + lines < n_array_size(candidates))
            j += lines;
        goto onemoretime;
    }

    poldek_term__ask_end();

    if (a == '\n')
        return hint;

    if (a == 'q' || a == 'Q')
        return POLDEK_ANSWER_ABORT;

    a = strchr(choice, a) - choice;
    if (a >= 0 && a < i)
        return a + j;

    return hint;
}

/* Y/n question of the suggests dialog, Enter taking the dialog's default;
   returns 'y', 'n', 'r' (redo) or 'q' */
static int term_confirm_suggest(const char *question, int hint)
{
    char prompt[512];

    n_snprintf(prompt, sizeof(prompt), "%s %s", question,
               hint ? "[Y/n/r/q]" : "[N/y/r/q]");
    return ask(prompt, "ynrq", hint ? 'y' : 'n');
}

static int term_choose_suggests(void *foo, const struct poldek_ts *ts,
                                const struct pkg *pkg, tn_array *caps,
                                tn_array *choices, int hint)
{
    char message[512], prompt[512], *question;
    char *yns = "N/y/s/r/q", *yn = "N/y/r/q";
    int i, n, a;

    foo = foo; ts = ts;

    if (!isatty(STDIN_FILENO))
        return hint;

    if (hint) {
        yns = "Y/n/s/r/q";
        yn = "Y/n/r/q";
    }

    n_snprintf(message, sizeof(message),
               _("Package %s suggests installation of:"), pkg_id(pkg));

    question = ngettext("Try to install it?", "Try to install them?",
                        n_array_size(caps));

onemoretime:
    n_array_clean(choices);

    msg_ask("%s\n", message);

    for (i=0; i < n_array_size(caps); i++) {
        struct capreq *cap = n_array_nth(caps, i);
        msgn(-1, "%d. %s", i + 1, capreq_stra(cap));
    }

    if (n_array_size(caps) > 1) {
        n = n_snprintf(prompt, sizeof(prompt), "%s ", question);
        n_snprintf(&prompt[n], sizeof(prompt) - n,
                   _("(y - all, n - nothing, s - select some of, r - redo, "
                     "q - abort)? [%s]"), yns);

        a = ask(prompt, "ynsrq", hint ? 'y' : 'n');
    } else {
        n_snprintf(prompt, sizeof(prompt), "%s [%s]", question, yn);

        a = ask(prompt, "ynrq", hint ? 'y' : 'n');
    }

    if (a == 'r')
        goto onemoretime;

    switch (a) {
        case 'y': a = 1; break;
        case 'n': a = 0; break;
        case 's': a = 2; break;
        case 'q': a = POLDEK_ANSWER_ABORT; break;
        default:
            n_assert(0);
    }

    if (a == 2) {
        for (i=0; i < n_array_size(caps); i++) {
            struct capreq *cap = n_array_nth(caps, i);
            char q[512];
            int sa;

            n_snprintf(q, sizeof(q), _("Try to install %s?"), capreq_stra(cap));

            sa = term_confirm_suggest(q, hint);
            if (sa == 'q')
                return POLDEK_ANSWER_ABORT;
            if (sa == 'r')
                goto onemoretime;
            if (sa == 'y')
                n_array_push(choices, cap);
        }
    }

    return a;
}

/* an answer of POLDEK_ANSWER_ABORT or out of the contract: cancels ts */
static int aborted(struct poldek_ts *ts)
{
    poldek__ts_cancel(ts);
    return POLDEK_ANSWER_ABORT;
}

int poldek__confirm(struct poldek_ts *ts, int hint, const char *message)
{
    int answer;

    if (ts->ctx->confirm_fn == NULL)
        return hint;

    answer = ts->ctx->confirm_fn(ts->ctx->data_confirm_fn, ts, hint, message);
    return answer >= 0 ? answer > 0 : aborted(ts);
}

int poldek__ts_confirm(struct poldek_ts *ts)
{
    int answer;

    if (ts->ctx->ts_confirm_fn == NULL)
        return 1;

    answer = ts->ctx->ts_confirm_fn(ts->ctx->data_ts_confirm_fn, ts);
    return answer >= 0 ? answer > 0 : aborted(ts);
}

int poldek__choose_equiv(struct poldek_ts *ts,
                         const struct pkg *pkg, const char *capname,
                         tn_array *pkgs, struct pkg *hint)
{
    int i, inthint = 0, answer;

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

    answer = ts->ctx->choose_equiv_fn(ts->ctx->data_choose_equiv_fn,
                                      ts, pkg, capname, pkgs, inthint);
    return answer >= 0 && answer < n_array_size(pkgs) ? answer : aborted(ts);
}

int poldek__choose_suggests(struct poldek_ts *ts,
                            const struct pkg *pkg, tn_array *caps,
                            tn_array *choices, int hint)
{
    int answer;

    if (ts->ctx->choose_suggests_fn == NULL)
        return hint;

    answer = ts->ctx->choose_suggests_fn(ts->ctx->data_choose_suggests_fn,
                                         ts, pkg, caps, choices, hint);
    return answer >= 0 && answer <= 2 ? answer : aborted(ts);
}


void poldek__setup_default_ask_callbacks(struct poldek_ctx *ctx)
{
    ctx->data_confirm_fn = NULL;
    ctx->confirm_fn = term_confirm;

    ctx->data_ts_confirm_fn = NULL;
    ctx->ts_confirm_fn = term_ts_confirm;

    ctx->data_choose_equiv_fn = NULL;
    ctx->choose_equiv_fn = term_choose_equiv;

    ctx->data_choose_suggests_fn = NULL;
    ctx->choose_suggests_fn = term_choose_suggests;
}
