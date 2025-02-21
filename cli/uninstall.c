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

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "cli.h"
#include "poldek_util.h"
#include "op.h"
#include "arg_packages.h"

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int uninstall(struct cmdctx *cmdctx);

#define OPT_GID             OPT_GID_OP_UNINSTALL
#define OPT_INST_NOASK      'y'
#define OPT_UNINSTALL       'e'
#define OPT_INST_NODEPS     (OPT_GID + 2)
#define OPT_INST_GREEDY     'g'
#define OPT_PM              (OPT_GID + 4)

static struct argp_option options[] = {
{"test", 't', 0, 0,
 N_("Do not remove, but tell if it would work or not"), OPT_GID },

{"noask", OPT_INST_NOASK, 0, 0, N_("Don't ask about anything"), OPT_GID },

{"nofollow", 'N', 0, 0, N_("Remove only selected packages"), OPT_GID },

{"nodeps", OPT_INST_NODEPS, 0, 0, N_("Ignore broken dependencies"), OPT_GID },

{"greedy", OPT_INST_GREEDY, "LEVEL", OPTION_ARG_OPTIONAL,
     N_("Remove packages required by selected ones if possible."), OPT_GID },

{"pmop", OPT_PM, "OPTION", 0,
 N_("pass option OPTION to PM binary (ex. --pmop noscripts)"), OPT_GID },
{"rpm", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, OPT_GID },
{"pmopt", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, OPT_GID },

{ 0, 0, 0, 0, 0, 0 },
};

static struct argp_option cmdl_options[] = {
    {0,0,0,0, N_("Package deinstallation:"), OPT_GID - 10 },
    {"erase", OPT_UNINSTALL, 0, 0, N_("Uninstall given packages"), OPT_GID - 10 },
    {"uninstall", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, 11 },
    { 0, 0, 0, 0, 0, 0 },
};


struct poclidek_cmd command_uninstall = {
    COMMAND_HASVERBOSE | COMMAND_MODIFIESDB |
    COMMAND_PIPEABLE_LEFT | COMMAND_PIPE_XARGS | COMMAND_PIPE_PACKAGES,
    "uninstall", N_("PACKAGE..."), N_("Uninstall packages"),
    options, parse_opt,
    NULL, uninstall, NULL, NULL, NULL, NULL, NULL, 0, 0,
    "remove"
};

static struct argp cmd_argp = {
    options, parse_opt, 0, 0, 0, 0, 0
};

static struct argp_child cmd_argp_child[2] = {
    { &cmd_argp, 0, NULL, 0 },
    { NULL, 0, NULL, 0 },
};

static
error_t cmdl_parse_opt(int key, char *arg, struct argp_state *state);

static struct argp poclidek_argp = {
    cmdl_options, cmdl_parse_opt, 0, 0, cmd_argp_child, 0, 0
};


static
struct argp_child poclidek_argp_child = {
    &poclidek_argp, 0, NULL, OPT_GID,
};


static int cmdl_run(struct poclidek_opgroup_rt *rt);

struct poclidek_opgroup poclidek_opgroup_uninstall = {
    "Package deinstallation",
    &poclidek_argp,
    &poclidek_argp_child,
    cmdl_run
};


struct cmdl_arg_s {
    struct cmdctx cmdctx;
};

static
error_t cmdl_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt  *rt;
    struct poldek_ts            *ts;
    struct cmdctx               cmdctx;


    rt = state->input;
    ts = rt->ts;
    arg = arg;

    cmdctx.ts = rt->ts;

    switch (key) {
        case ARGP_KEY_INIT:
            state->child_inputs[0] = &cmdctx;
            state->child_inputs[1] = NULL;
            break;

        case OPT_UNINSTALL:
            poldek_ts_set_type(ts, POLDEK_TS_TYPE_UNINSTALL, "-e");
            rt->set_major_mode(rt, "erase", NULL);
            break;

        default:
            return ARGP_ERR_UNKNOWN;

    }

    return 0;
}

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx     *cmdctx = state->input;
    struct poldek_ts  *ts = cmdctx->ts;

    arg = arg;
    switch (key) {
        //case 'm':
        //ts->setop(ts, POLDEK_OP_VRFYMERCY, 1);
        //    break;

        case OPT_INST_NOASK:
            ts->setop(ts, POLDEK_OP_CONFIRM_UNINST, 0);
            break;

        case OPT_INST_NODEPS:
            ts->setop(ts, POLDEK_OP_NODEPS, 1);
            break;

        case OPT_INST_GREEDY:
            if (!arg) {
                ts->setop(ts, POLDEK_OP_GREEDY, 1);
            } else if (*arg == 'g') { /* -ggg form */
                int v = 1;
                while (*arg++ == 'g')
                    v++;
                ts->setop(ts, POLDEK_OP_GREEDY, v);
            } else {
                int v, val;

                if (sscanf(arg, "%u", &v) == 1) {
                    val = v;

                } else if ((val = poldek_util_parse_bool(arg)) == -1) {
                    logn(LOGERR, _("invalid value ('%s') of option 'greedy'"),
                         arg);
                    return EINVAL;
                }

                ts->setop(ts, POLDEK_OP_GREEDY, val);
            }
            break;

        case 't':
            if (ts->getop(ts, POLDEK_OP_TEST))
                ts->setop(ts, POLDEK_OP_RPMTEST, 1);
            else
                ts->setop(ts, POLDEK_OP_TEST, 1);
            break;

        case 'N':
            ts->setop(ts, POLDEK_OP_FOLLOW, 0);
            break;

        case OPT_PM: {
            tn_array *tl = NULL;
            int i;

            if ((tl = n_str_etokl_ext(arg, "\t ", "", "\"'", '\\')) == NULL) {
                logn(LOGERR, _("%s: parse error"), arg);
                return ARGP_ERR_UNKNOWN;
            }

            for (i=0; i < n_array_size(tl); i++) {
                char *a, opt[256], *dash = "--";

                a = n_array_nth(tl, i);
                if (*a == '-')
                    dash = "";

                n_snprintf(opt, sizeof(opt), "%s%s", dash, a);
                poldek_ts_configure(ts, POLDEK_CONF_RPMOPTS, opt);
            }

            n_array_cfree(&tl);
        }
            break;


        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}


static int uninstall(struct cmdctx *cmdctx)
{
    struct poclidek_ctx  *cctx;
    struct poldek_ts     *ts;
    tn_array             *pkgs;
    int                  i, err = 0;
    unsigned             ts_flags = 0;

    cctx = cmdctx->cctx;
    ts = cmdctx->ts;

    /* must be resolved here to allow globbing, RPM doesn't support globs AFAIK */
    pkgs = poclidek_resolve_packages(POCLIDEK_INSTALLEDDIR, cctx, ts,
                                     ARG_PACKAGES_RESOLV_EXACT,
                                     PKG_DENT_LDFIND_STUBSOK);
    if (pkgs == NULL) {
        err++;
        goto l_end;
    }

    poldek_ts_clean_args(ts);
    for (i=0; i < n_array_size(pkgs); i++) {
        DBGF("%s\n", pkg_id(n_array_nth(pkgs, i)));
        poldek_ts_add_pkg(ts, n_array_nth(pkgs, i));
    }
    n_array_free(pkgs);

    if (!ts->getop_v(ts, POLDEK_OP_TEST, POLDEK_OP_RPMTEST, 0))
        ts_flags |= POLDEK_TS_TRACK;

    poldek_ts_set_type(ts, POLDEK_TS_TYPE_UNINSTALL, "uninstall");
    if (!poldek_ts_run(ts, ts_flags))
        err++;

    if (ts_flags)
        poclidek_apply_iinf(cmdctx->cctx, ts);

l_end:
    return err == 0;
}


static int cmdl_run(struct poclidek_opgroup_rt *rt)
{
    int rc;

    if (poldek_ts_get_type(rt->ts) != POLDEK_TS_UNINSTALL)
        return OPGROUP_RC_NIL;

    rc = poldek_ts_run(rt->ts, 0);
    return rc ? OPGROUP_RC_OK : OPGROUP_RC_ERROR;
}
