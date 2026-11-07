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

#define CTRL_C 0x03             /* a key, as the terminal is in raw mode */

/* A key typed at a question; q for ^C and when the terminal is gone (a
   read with SIGHUP ignored) */
static int getkey(void)
{
    int a = poldek_term__getkey();

    return a == CTRL_C || a == -1 ? 'q' : a;
}

/* An answer to a question: its key, what it does in a few words, shown in
   the question, and at more length, shown by ? (NULL: the same) */
struct answer {
    int         key;
    const char  *meaning;
    const char  *help;
};

static int is_answer(const struct answer *answers, int key)
{
    int i;

    for (i = 0; answers[i].key; i++)
        if (answers[i].key == key)
            return 1;

    return 0;
}

/* what q does, for ? */
static const char *abort_help(const struct poldek_ts *ts)
{
    if (ts->type != POLDEK_TS_UNINSTALL && ts->getop(ts, POLDEK_OP_PARTICLE))
        return _("abort this command; sets installed before stay installed");

    return _("abort this command");
}

/* Asks question with answers (the last with key 0), naming them, and waits
   for one of their keys, in either case; Enter gives defkey, ? the help, ^C
   q. Echoes the answer and returns its key. */
static int ask(const char *question, const struct answer *answers, int defkey)
{
    char prompt[4096];
    const char *again;
    int i, n, a;

    n_assert(is_answer(answers, 'q'));

    n = n_snprintf(prompt, sizeof(prompt), "%s (", question);
    for (i = 0; answers[i].key; i++)
        n += n_snprintf(&prompt[n], sizeof(prompt) - n, "%c - %s, ",
                        answers[i].key, answers[i].meaning);

    n += n_snprintf(&prompt[n], sizeof(prompt) - n, "? - %s) [", _("help"));
    for (i = 0; answers[i].key; i++)
        n += n_snprintf(&prompt[n], sizeof(prompt) - n, "%c/",
                        answers[i].key == defkey ? toupper(defkey) : answers[i].key);
    n_snprintf(&prompt[n], sizeof(prompt) - n, "?]");

    /* asked again: the question's last line, not the text above it */
    again = strrchr(prompt, '\n');
    again = again ? again + 1 : prompt;

    poldek_term__ask_begin(STDIN_FILENO);
    msg_ask("%s", prompt);

    while ((a = getkey()) != '\n' && !is_answer(answers, tolower(a))) {
        if (a == '?') {
            msg_ask("_ ?\n");
            for (i = 0; answers[i].key; i++)
                msg_ask("  %c - %s\n", answers[i].key,
                        answers[i].help ? answers[i].help : answers[i].meaning);
            msg_ask("%s", again);

        } else if (isprint(a)) {
            msg_ask("_ %c\n", a);
            msg_ask(_("'%c' is not an answer here; ? - help\n"), a);
            msg_ask("%s", again);
        }
    }
    poldek_term__ask_end();

    a = (a == '\n') ? defkey : tolower(a);

    msg_ask("_ %c\n", a);
    return a;
}

/* the question with y and n doing what yes and no say, and q; returns 1,
   0 or POLDEK_ANSWER_ABORT */
static int ask_yes_no(const struct poldek_ts *ts, const char *question,
                      int hint, const char *yes, const char *no,
                      const char *no_help)
{
    struct answer answers[] = {
        { 'y', yes, NULL },
        { 'n', no, no_help },
        { 'q', _("abort"), abort_help(ts) },
        { 0, NULL, NULL },
    };

    if (!isatty(STDIN_FILENO))
        return hint;

    switch (ask(question, answers, hint ? 'y' : 'n')) {
        case 'y': return 1;
        case 'n': return 0;
        default:  return POLDEK_ANSWER_ABORT;
    }
}

static int term_confirm(void *foo, const struct poldek_ts *ts, int hint,
                        const char *question)
{
    foo = foo;
    return ask_yes_no(ts, question, hint, _("yes"), _("no"), NULL);
}

static int term_ts_confirm(void *foo, const struct poldek_ts *ts)
{
    const char *yes, *no, *no_help;
    char help[512];

    foo = foo;

    /* poldek__ts_display_summary(ts); */ /* already displayed */

    if (ts->type == POLDEK_TS_UNINSTALL) {
        if (!ts->getop(ts, POLDEK_OP_CONFIRM_UNINST))
            return 1;

        /* no one to confirm it: removing nothing is a failure, not an n */
        if (!isatty(STDIN_FILENO)) {
            logn(LOGERR, _("nothing removed: no terminal to confirm the removal "
                           "on (--noask removes without asking)"));
            return POLDEK_ANSWER_ABORT;
        }

        return ask_yes_no(ts, _("Proceed?"), 0, _("remove"), _("don't remove"),
                          NULL);
    }

    /* a provider chosen here may be chosen again, confirmed or not */
    if (!ts->getop(ts, POLDEK_OP_CONFIRM_INST) && !(ts->_iflags & TS_CHOSEN))
        return 1;

    if (!ts->getop(ts, POLDEK_OP_PARTICLE)) {
        yes = _("install");
        no = _("don't install");
        no_help = NULL;

    } else {
        n_assert(ts->_set_head);
        n_snprintf(help, sizeof(help),
                   _("skip the set of %s, go on with the next one"),
                   pkg_id(ts->_set_head));

        yes = _("install this set");
        no = _("skip it");
        no_help = help;
    }

    if (!(ts->_iflags & TS_CHOSEN))
        return ask_yes_no(ts, _("Proceed?"), 1, yes, no, no_help);

    {
        struct answer answers[] = {
            { 'y', yes, NULL },
            { 'n', no, no_help },
            { 'r', _("choose again"),
              _("forget the providers chosen and ask again") },
            { 'q', _("abort"), abort_help(ts) },
            { 0, NULL, NULL },
        };

        switch (ask(_("Proceed?"), answers, 'y')) {
            case 'y': return 1;
            case 'n': return 0;
            case 'r': return POLDEK_ANSWER_REDO;
            default:  return POLDEK_ANSWER_ABORT;
        }
    }
}

/* the letters of a page, n of them, as ranges: "a-p, r-z, 1-9, 0, A-C" */
static void letter_ranges(char *buf, int size, const char *letters, int n)
{
    int i = 0, k = 0;

    buf[0] = '\0';
    while (i < n) {
        int e = i;

        while (e + 1 < n && letters[e + 1] == letters[e] + 1)
            e++;

        if (k > 0)
            k += n_snprintf(&buf[k], size - k, ", ");

        if (e == i)
            k += n_snprintf(&buf[k], size - k, "%c", letters[i]);
        else
            k += n_snprintf(&buf[k], size - k, "%c-%c", letters[i], letters[e]);
        i = e + 1;
    }
}

static int term_choose_equiv(void *foo, const struct poldek_ts *ts,
                             const struct pkg *pkg, const char *capname,
                             tn_array *candidates, int hint)
{
    char validchrs[100], ranges[128], defkey[256], prompt[512], none[512], *p;
    const char *skip;
    int i, j, a, lines;
    /* n and N skip, q and Q abort */
    char choice[] = "abcdefghijklmoprstuvwxyz1234567890ABCDEFGHIJKLMOPRSTUVWXYZ";

    foo = foo;
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

    if (pkg == NULL) {          /* a capability asked for (--caplookup) */
        skip = _("leave it out");
        n_snprintf(none, sizeof(none), _("install none of them, leave \"%s\" out"),
                   capname);

    } else if (ts->getop(ts, POLDEK_OP_PARTICLE)) {
        n_assert(ts->_set_head);
        skip = _("skip this set");
        n_snprintf(none, sizeof(none), _("install none of them, skip the set "
                                         "of %s; later sets that need \"%s\" "
                                         "are skipped too"),
                   pkg_id(ts->_set_head), capname);
    } else {
        skip = _("install nothing");
        n_snprintf(none, sizeof(none), "%s",
                   _("install none of them, nor anything else"));
    }

onemoretime:
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

    *p++ = 'n';
    *p++ = 'N';
    *p++ = 'q';
    *p++ = 'Q';
    *p = '\0';

    letter_ranges(ranges, sizeof(ranges), choice, i);
    if (hint >= j && hint < j + i)
        n_snprintf(defkey, sizeof(defkey), "%c", choice[hint - j]);
    else
        n_snprintf(defkey, sizeof(defkey), "%s",
                   pkg_id(n_array_nth(candidates, hint)));

    n_snprintf(prompt, sizeof(prompt),
               _("Which one do you want to install? (%s - that package, "
                 "Enter - %s, n - %s, q - abort, ? - help)"), ranges, defkey,
               skip);
    msg_ask("%s", prompt);

    while (1) {
        a = getkey();

        if (a == '?') {
            msg_ask("_ ?\n");
            msg_ask("  %s - %s\n", ranges, _("install that package"));
            msg_ask("  Enter - %s\n",
                    pkg_id(n_array_nth(candidates, hint)));
            msg_ask("  n - %s\n", none);
            msg_ask("  q - %s\n", abort_help(ts));

        } else if (a != '\0' && strchr(validchrs, a)) {
            break;

        } else if (isprint(a)) {
            msg_ask("_ %c\n", a);
            msg_ask(_("'%c' is not an answer here; ? - help\n"), a);

        } else {
            continue;
        }

        msg_ask("%s", prompt);
    }

    if (a == '-' || a == 0x7f) {
        msg_ask("_\n");
	if (j >= lines)
            j -= lines;
        goto onemoretime;
    }

    if (a == '+' || a == ' ' || a == '\t' ) {
        msg_ask("_\n");
	if (j + lines < n_array_size(candidates))
            j += lines;
        goto onemoretime;
    }

    poldek_term__ask_end();

    if (a == 'n' || a == 'N') {
        msg_ask("_ n\n");
        return POLDEK_ANSWER_SKIP;
    }

    if (a == 'q' || a == 'Q') {
        msg_ask("_ q\n");
        return POLDEK_ANSWER_ABORT;
    }

    if (a != '\n')
        hint = strchr(choice, a) - choice + j;

    if (hint >= j && hint < j + i)
        msg_ask("_ %c) %s\n", choice[hint - j],
                pkg_id(n_array_nth(candidates, hint)));
    else
        msg_ask("_ %s\n", pkg_id(n_array_nth(candidates, hint)));

    return hint;
}

/* y/n question of the suggests dialog about one of them, Enter taking the
   dialog's default; returns 'y', 'n', 'r' (back to the list) or 'q' */
static int term_confirm_suggest(const struct poldek_ts *ts,
                                const char *question, int hint)
{
    struct answer answers[] = {
        { 'y', _("install it"), NULL },
        { 'n', _("don't"), NULL },
        { 'r', _("redo"), _("start again from the list") },
        { 'q', _("abort"), abort_help(ts) },
        { 0, NULL, NULL },
    };

    return ask(question, answers, hint ? 'y' : 'n');
}

static int term_choose_suggests(void *foo, const struct poldek_ts *ts,
                                const struct pkg *pkg, tn_array *caps,
                                tn_array *choices, int hint)
{
    struct answer all[] = {
        { 'y', _("all"), _("install all of them") },
        { 'n', _("none"), _("install none of them") },
        { 's', _("select some"), _("ask about each of them") },
        { 'r', _("redo"), _("show the list again") },
        { 'q', _("abort"), abort_help(ts) },
        { 0, NULL, NULL },
    };
    struct answer one[] = {
        { 'y', _("install it"), NULL },
        { 'n', _("don't"), NULL },
        { 'r', _("redo"), _("show the list again") },
        { 'q', _("abort"), abort_help(ts) },
        { 0, NULL, NULL },
    };
    char message[512], *question;
    int i, a;

    foo = foo;

    if (!isatty(STDIN_FILENO))
        return hint;

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

    a = ask(question, n_array_size(caps) > 1 ? all : one, hint ? 'y' : 'n');

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

            sa = term_confirm_suggest(ts, q, hint);
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

int poldek__confirm(struct poldek_ts *ts, int hint, const char *message,
                    const char *yes, const char *no)
{
    int answer;

    if (ts->ctx->confirm_fn == NULL)
        return hint;

    /* ours names what y and n do, which the callback has no arguments for */
    if (ts->ctx->confirm_fn == term_confirm)
        answer = ask_yes_no(ts, message, hint, yes, no, NULL);
    else
        answer = ts->ctx->confirm_fn(ts->ctx->data_confirm_fn, ts, hint,
                                     message);

    return answer >= 0 ? answer > 0 : aborted(ts);
}

int poldek__ts_confirm(struct poldek_ts *ts)
{
    int answer;

    if (ts->ctx->ts_confirm_fn == NULL)
        return 1;

    answer = ts->ctx->ts_confirm_fn(ts->ctx->data_ts_confirm_fn, ts);
    if (answer == POLDEK_ANSWER_REDO && ts->type != POLDEK_TS_UNINSTALL)
        return answer;

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

    /* ours asks on a terminal only */
    if (ts->ctx->choose_equiv_fn == term_choose_equiv && isatty(STDIN_FILENO))
        ts->_iflags |= TS_CHOSEN;

    answer = ts->ctx->choose_equiv_fn(ts->ctx->data_choose_equiv_fn,
                                      ts, pkg, capname, pkgs, inthint);
    if (answer == POLDEK_ANSWER_SKIP)
        return answer;

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
