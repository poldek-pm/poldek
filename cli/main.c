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
#include <string.h>
#include <sys/param.h>

#include <argp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include <vfile/vfile.h>

#include "sigint/sigint.h"
#include "i18n.h"
#include "log.h"
#include "pkgdir/source.h"
#include "conf.h"
#include "poldek.h"
#include "cmd_chain.h"
#include "cli.h"
#include "op.h"

#ifndef VERSION
# error "undefined VERSION"
#endif

extern int poclidek_shell(struct poclidek_ctx *cctx);

const char *argp_program_version = poldek_VERSION_BANNER;
const char *argp_program_bug_address = poldek_BUG_MAILADDR;
static char args_doc[] = N_("[PACKAGE...]");

/* FIXME: only way to disable --version opt in argp
   is set global argp_program_version to null  */
static void disable_version_opt_and_bug_address() {
    argp_program_version = NULL;
    argp_program_bug_address = NULL;
}

#if GENDOCBOOK
static void argp_as_docbook(struct argp *argp);
#endif

#define OPT_GID       OPT_GID_OP_OTHER

#define OPT_CACHEDIR          (OPT_GID + 1)
#define OPT_ASK               (OPT_GID + 2)
#define OPT_NOASK             'y'
#define OPT_CONF              (OPT_GID + 4)
#define OPT_UPCONF            (OPT_GID + 5)
#define OPT_NOCONF            (OPT_GID + 6)
#define OPT_BANNER            (OPT_GID + 7)
#define OPT_LOG               (OPT_GID + 8)
#define OPT_SKIPINSTALLED     (OPT_GID + 9)
#define OPT_PM                (OPT_GID + 11)
#define OPT_PMCMD             (OPT_GID + 12)
#define OPT_PMCMD_ALIAS       (OPT_GID + 13)
#define OPT_SUDOCMD           (OPT_GID + 15)
#define OPT_SHELL             (OPT_GID + 16)
#define OPT_SHELL_CMD         (OPT_GID + 17)
#define OPT_RUNAS             (OPT_GID + 18)
#define OPT_OPTION 'O'
#define OPT_SHCMD             (OPT_GID + 19)
#define OPT_NOPROGRESS          (OPT_GID + 20)
#define OPT_FORCECOLOR        (OPT_GID + 21)
#define OPT_DOCB              (OPT_GID + 24)

#define OPT_AS_FLAG(OPT)       (1 << (OPT - OPT_GID))

/* The options we understand. */
static struct argp_option common_options[] = {
{0,0,0,0, N_("Miscellaneous options:"), OPT_GID },
{"pm", OPT_PM, "PM", OPTION_HIDDEN, 0, OPT_GID },
{"pmcmd", OPT_PMCMD, "FILE", 0, N_("Use FILE as PM(rpm) binary"), OPT_GID },
{"rpmcmd", OPT_PMCMD_ALIAS, "FILE", OPTION_HIDDEN, 0, OPT_GID },
{"sudocmd", OPT_SUDOCMD, "FILE", 0, N_("Use FILE as sudo binary"), OPT_GID },

{"cachedir", OPT_CACHEDIR, "DIR", 0,
     N_("Store downloaded files and co. under DIR"), OPT_GID },

{"cmd", OPT_SHCMD, 0, 0,
     N_("Run in command mode (like ipoldek does by default)"), OPT_GID },

{"ask", OPT_ASK, 0, 0, N_("Confirm packages installation and "
                          "let user choose among equivalent packages"), OPT_GID },
{"noask", OPT_NOASK, 0, 0, N_("Don't ask about anything"), OPT_GID },

{"shell", OPT_SHELL, 0, 0, N_("Run in interactive mode (default)"), OPT_GID },

{"shcmd", OPT_SHELL_CMD, "COMMAND", OPTION_HIDDEN,
                 N_("Run poldek shell COMMAND and exit"), OPT_GID },

{"skip-installed", OPT_SKIPINSTALLED, 0, 0,
     N_("Don't load installed packages at startup"), OPT_GID },
{"fast", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, NULL, OPT_GID }, /* legacy */
{ 0, 'f', 0, OPTION_ALIAS | OPTION_HIDDEN, NULL, OPT_GID }, /* legacy */

{"conf", OPT_CONF, "FILE", 0, N_("Read configuration from FILE"), OPT_GID },
{"_conf", 'c', "FILE", OPTION_HIDDEN,
     N_("Read configuration from FILE"), OPT_GID }, /* legacy */

{"noconf", OPT_NOCONF, 0, 0, N_("Do not read configuration"), OPT_GID },
{"upconf", OPT_UPCONF, 0, 0, N_("Update remote configuration files (if any)"),
     OPT_GID },

{"version", OPT_BANNER, 0, OPTION_HIDDEN,
     N_("Display program version information and exit"), OPT_GID },

{"log", OPT_LOG, "FILE", 0, N_("Log program messages to FILE"), OPT_GID },
{"runas", OPT_RUNAS, "USER", 0, N_("Run program as user USER"), OPT_GID },
{NULL, OPT_OPTION, "OPTION=VALUE", 0, N_("Set configuration option"), OPT_GID },
{"docbook", OPT_DOCB, 0, OPTION_HIDDEN,
        N_("Dump options in docbook format"), OPT_GID },
{"noprogress", OPT_NOPROGRESS, 0, 0, N_("Do not show progress bar"), OPT_GID },
{"color", OPT_FORCECOLOR, 0, 0, N_("Force color on non-tty output"), OPT_GID },
{0,  'v', 0, 0, N_("Be verbose."), OPT_GID },
{0,  'q', 0, 0, N_("Do not produce any output."), OPT_GID },
{ 0, 0, 0, 0, 0, 0 },
};

#define RUNMODE_POLDEK       0
#define RUNMODE_APT          1

#define MODE_NULL         0
#define MODE_VERIFY       1
#define MODE_MKIDX        2
#define MODE_INSTALL      4
#define MODE_INSTALLDIST  5
#define MODE_UNINSTALL    6
#define MODE_SRCLIST      8
#define MODE_SHELL        9

#define MODE_F_LDSOURCES  (1 << 0)

static struct poclidek_opgroup *poclidek_opgroup_tab[] = {
    &poclidek_opgroup_source,
    &poclidek_opgroup_packages,
    &poclidek_opgroup_install,
    &poclidek_opgroup_uninstall,
    &poclidek_opgroup_makeidx,
    &poclidek_opgroup_verify,
    NULL
};

struct args {
    struct poldek_ctx    *ctx;
    struct poclidek_ctx *cctx;
    struct poldek_ts     *ts;

    struct poclidek_op_ctx *opctx;

    int       mode;
    int       mjrmode;
    unsigned  mnrmode;

    unsigned  cnflags;

    tn_array    *addon_cnflines;
    char        *path_conf;
    char        *path_log;

    char        *shcmd;

    tn_array    *opgroup_rts;

    int         argc;
    char        **argv;
} g_args;

static
void set_config_option(struct args *argsp, const char *op, const char *val)
{
    char tmp[1024];

    if (argsp->addon_cnflines == NULL)
        argsp->addon_cnflines = n_array_new(8, free, NULL);

    n_snprintf(tmp, sizeof(tmp), "%s = %s", op, val);
    n_array_push(argsp->addon_cnflines, n_strdup(tmp));
}

/* Parse a single option. */
static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct args *argsp = state->input;
    struct poldek_ctx *ctx = argsp->ctx;
    struct poldek_ts *ts = argsp->ts;
//    if (key && arg)
//        chkarg(key, arg);

    switch (key) {
        case ARGP_KEY_INIT:
            {
                int i = 0;
                for (i=0; i < n_array_size(argsp->opgroup_rts); i++)
                    state->child_inputs[i] = n_array_nth(argsp->opgroup_rts, i);
            }
            break;

        case OPT_LOG:
            poldek_configure(ctx, POLDEK_CONF_LOGFILE, arg);
            break;

        case OPT_CACHEDIR:
            poldek_configure(ctx, POLDEK_CONF_CACHEDIR, arg);
            if (!poldek_setup_cachedir(argsp->ctx)) /* set up immediately */
                exit(EXIT_FAILURE);
            break;

        case 'c':
            logn(LOGNOTICE, _("-c is depreciated, use --conf instead"));
            /* fallthru */

        case OPT_CONF:
            g_args.path_conf = n_strdup(arg);
            break;

        case OPT_UPCONF:
            argsp->cnflags |= OPT_AS_FLAG(OPT_UPCONF);
            break;

        case 'q':
            poldek_set_verbose(-1);
            break;

        case OPT_SHCMD:
            argsp->mode = RUNMODE_APT;
            break;

        case 'v':
            poldek_up_verbose();
            break;

        case OPT_NOCONF:
            argsp->cnflags |= OPT_AS_FLAG(OPT_NOCONF);
            break;

        case OPT_ASK:
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 1);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 1);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_EQPKG_ASKUSER, 1);
            break;

        case OPT_NOASK:
            // OPT_NOASK is not used as flag (probably...)
            //argsp->cnflags |= OPT_AS_FLAG(OPT_NOASK);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 0);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 0);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_EQPKG_ASKUSER, 0);
            break;

        case OPT_RUNAS:         /* ignored, catched at startup */
            break;

        case OPT_SHELL:         /* default */
            argsp->mjrmode = MODE_SHELL;
            argsp->cnflags |= OPT_AS_FLAG(OPT_SHELL);
            break;

        case OPT_SHELL_CMD:
            argsp->shcmd = arg;
            argsp->mjrmode = MODE_SHELL;
            argsp->cnflags |= OPT_AS_FLAG(OPT_SHELL);
            break;

        case 'f':
            logn(LOGWARN, "-f is obsoleted, use --skip-installed instead");
            /* fallthru */

        case OPT_SKIPINSTALLED:
            argsp->cnflags |= OPT_AS_FLAG(OPT_SKIPINSTALLED);
            argsp->cctx->flags |= POCLIDEK_SKIP_INSTALLED;
            break;

        case OPT_PM:
            poldek_configure(ctx, POLDEK_CONF_PM, arg);
            break;

        case OPT_PMCMD_ALIAS:
        case OPT_PMCMD:
            set_config_option(argsp, "pm command", arg);
            break;

        case OPT_SUDOCMD:
            set_config_option(argsp, "sudo command", arg);
            break;

        case OPT_BANNER:
            msgn(-1, "%s", poldek_VERSION_BANNER);
            exit(EXIT_SUCCESS);
            break;

        case OPT_OPTION:
            if (argsp->addon_cnflines == NULL)
                argsp->addon_cnflines = n_array_new(8, free, NULL);
            n_array_push(argsp->addon_cnflines, n_strdup(arg));
            break;

        case OPT_DOCB:
            argsp->cnflags |= OPT_AS_FLAG(OPT_DOCB);
            break;

        case 'G':               /* XXX have no idea why this case from install.c
                                   does not work */
            ts->setop(ts, POLDEK_OP_GREEDY, 1);
            break;

       case OPT_NOPROGRESS:
    	    ts->setop(ts, POLDEK_OP_PROGRESS_NONE, 1);
    	    poldek_configure(ctx, POLDEK_CONF_PROGRESS, arg);
    	    break;

       case OPT_FORCECOLOR:
    	    poldek_configure(ctx, POLDEK_CONF_FORCECOLOR, 1);
    	    break;

        case ARGP_KEY_ARG:
            /* auto detect apt mode */
            if (!poclidek_op_ctx_has_major_mode(argsp->opctx)) {
                //int has_command = poclidek_has_batch_command(argsp->cctx, arg);
                //if (has_command) {
                //printf("AUTOAPT %s\n", arg);
                argsp->mode = RUNMODE_APT;
                //}
            }

            if (argsp->mode == RUNMODE_APT) { /* eat all args and keep it for shell */
                disable_version_opt_and_bug_address();

                argsp->argv[argsp->argc++] = arg;
                while (state->next < state->argc) {
                    char *a = state->argv[state->next++];
                    argsp->argv[argsp->argc++] = a;
                }
                argsp->argv[argsp->argc] = NULL;
                n_assert(state->next = state->argc);
                break;
            }
            /* fallthru */

        default:
            return ARGP_ERR_UNKNOWN;
            break;
    }

    return 0;
}

/* hides duplicate options as interactive and cmdl opt set may share some
   TODO: !O(n^2) */
static
void hide_child_options(const struct argp *parent, const struct argp *child)
{
    int i = 0;

    while (1) {
        int j, key;
        const struct argp_option *opt = &parent->options[i++];
        if (opt->name == NULL && opt->key == 0 && opt->doc == NULL)
            break;
        DBGF("%d) %d %s\n", i + 1, opt->key, opt->name);

        if (opt->key == 0)
            continue;

        key = opt->key;
        j = 0;
        while (1) {
            struct argp_option *copt = (struct argp_option *)&child->options[j++];
            if (copt->name == NULL && copt->key == 0 && copt->doc == NULL)
                break;

            DBGF("  %d. %d %s\n", j + 1, copt->key, copt->name);
            if (key && key == copt->key) {
                DBGF("Hide %d %s (%s)\n", copt->key, copt->name, copt->doc);
                copt->flags |= OPTION_HIDDEN;
            }

        }
    }
}

static
void argp_prepare_child_options(const struct argp *argp, int hide_child_opts)
{
    int i;
    const struct argp *child;

    if (argp->children == NULL)
        return;

    i = 0;
    while ((child = argp->children[i++].argp)) {
        if (hide_child_opts)
            hide_child_options(argp, child);
        argp_prepare_child_options(child, hide_child_opts);
    }
}


static int load_conf(struct args *argsp)
{
    unsigned flags = 0;

    if (argsp->cnflags & OPT_AS_FLAG(OPT_NOCONF))
        flags |= POLDEK_LOADCONF_NOCONF;

    else if (argsp->cnflags & OPT_AS_FLAG(OPT_UPCONF))
        flags |= POLDEK_LOADCONF_UPCONF;

    if ((flags & POLDEK_LOADCONF_NOCONF) && argsp->addon_cnflines == NULL)
        return 1;

    return poldek_load_config(argsp->ctx, argsp->path_conf,
                              argsp->addon_cnflines, flags);
}

static void args_init(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                      int argc, char **argv, int mode)
{
    memset(&g_args, 0, sizeof(g_args));

    g_args.ctx = cctx->ctx;
    g_args.cctx = cctx;

    g_args.argc = 0;
    g_args.argv = n_malloc(sizeof(*argv) * argc);
    g_args.argv[0] = NULL;

    g_args.mode = mode;
    g_args.ts = ts;
    g_args.opctx = poclidek_op_ctx_new();
}


static void preset_quiet(int argc, char **argv)
{
    for (int i=0; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            poldek_set_verbose(-1);
        }
    }
}

static
void parse_options(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                   int argc, char **argv, int mode)
{
    struct argp argp = { common_options, parse_opt, args_doc, 0, 0, 0, 0 };
    int n, i, index, hide_child_opts = 0;
    struct argp_child *child;

    poldek_set_verbose(0);
    preset_quiet(argc, argv); /* be quiet early (w/o for -v ... -q) */

    args_init(cctx, ts, argc, argv, mode);

    n = 0;
    while (poclidek_opgroup_tab[n])
        n++;

    child = alloca((n + 2) * sizeof(*child));
    g_args.opgroup_rts = n_array_new(n, (tn_fn_free)poclidek_opgroup_rt_free, NULL);

    for (i=0; i < n; i++) {
        struct poclidek_opgroup_rt *rt;
        child[i] = *(poclidek_opgroup_tab[i]->argp_child);
        rt = poclidek_opgroup_rt_new(g_args.ts, g_args.opctx);
        rt->run = poclidek_opgroup_tab[i]->run;
        n_array_push(g_args.opgroup_rts, rt);
    }
    child[i].argp = NULL;
    argp.children = child;

    if (poclidek_argv_is_help(argc, (const char **)argv))
        hide_child_opts = 1;


#if GENDOCBOOK
        hide_child_opts = 1;
#endif

    argp_prepare_child_options(&argp, hide_child_opts);

    index = 0;
    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, &index, &g_args);

    if (!poclidek_op_ctx_verify_major_mode(g_args.opctx))
        exit(EXIT_FAILURE);

    if (!load_conf(&g_args))
        exit(EXIT_FAILURE);

#if GENDOCBOOK
    if (args.cnflags & OPT_AS_FLAG(OPT_DOCB)) {
        argp_as_docbook(&argp);
        exit(EXIT_SUCCESS);
    }
#endif
    return;
}


int do_run(void)
{
    int i, all_rc = OPGROUP_RC_NIL;

    n_assert(g_args.opgroup_rts);

    for (i=0; i < n_array_size(g_args.opgroup_rts); i++) {
        struct poclidek_opgroup_rt *rt = n_array_nth(g_args.opgroup_rts, i);
        int rc;

        if (rt->run == NULL)
            continue;

        rc = rt->run(rt);
        all_rc |= rc;

        if (rc & OPGROUP_RC_ERROR)
            break;
    }

    n_array_free(g_args.opgroup_rts);
    g_args.opgroup_rts = NULL;

    return all_rc;
}

extern int poldek_su(const char *user);

static int do_su(int argc, char **argv)
{
    const char *oprunas = "--runas";
    char *user = NULL;
    int rc, i, n, is_runas = 0, oprunas_len, verbose = 0, noautosu = 0;
    char *nosu_opts[] = { "--mkidx", "--makeidx", NULL };
    int  nosu_opts_len[] = { 0, 0, 0 };

    n = 0;
    while (nosu_opts[n]) {
        nosu_opts_len[n] = strlen(nosu_opts[n]);
        n++;
    }

    oprunas_len = strlen(oprunas);
    for (i=1; i < argc; i++) {
        n = 0;
        while (noautosu == 0 && nosu_opts[n]) {
            if (strncmp(argv[i], nosu_opts[n], nosu_opts_len[n]) == 0)
                noautosu = 1;
            n++;
        }

        if (strncmp(argv[i], "-v", 2) == 0) {
            char *p = argv[i] + 1;
            while (*p) {
                if (*p == 'v')
                    verbose++;
                else if (*p) {  /* non-'v' => is is not -v[v...] option */
                    verbose = 0;
                    break;
                }
                p++;
            }
        }

        if (!is_runas && strncmp(argv[i], oprunas, oprunas_len) == 0) {
            char *p = argv[i] + oprunas_len;
            if (*p == '=') {
                p++;
                user = n_strdup(p);

            } else {            /* next arg? */
                if (i < argc - 1)
                    user = n_strdup(argv[i + 1]);
            }
            is_runas = 1;
        }
    }


    if (is_runas) {
        if (user == NULL) {
            logn(LOGERR, _("%s: option '%s' requires an argument\n"),
                 n_basenam(argv[0]), oprunas);
            return 0;
        } else if (getuid() != 0) {
            logn(LOGERR, _("%s: option '%s' gives no effect if program executed"
                           " by ordinary user"),
                 n_basenam(argv[0]), oprunas);
            return 0;
        }

    } else if (noautosu == 0 && getuid() == 0) {  /* check config's runas */
        tn_hash *cnf;

        cnf = poldek_conf_load_default(POLDEK_LDCONF_GLOBALONLY |
                                       POLDEK_LDCONF_NOVALIDATE);
        if (cnf) {
            tn_hash *global;
            const char *u;

            global = poldek_conf_get_section(cnf, "global");
            if (global && (u = poldek_conf_get(global, "run_as", NULL))) {
                user = n_strdup(u);
                is_runas = 1;
            }
            n_hash_free(cnf);
        }
    }

    if (!is_runas)
        return 1;

    n_assert(user);
    if (*user == '\0' || strcmp(user, "none") == 0) /* empty or 'none' => ret */
        return 1;

    verbose = poldek_set_verbose(verbose);
    rc = poldek_su(user);
    free(user);
    poldek_set_verbose(verbose);
    return rc;
}

static int run_poldek(struct poclidek_ctx *cctx)
{
    int rc;

    if (g_args.shcmd) {
        // batch mode
        rc = poclidek_execline(cctx, g_args.ts, g_args.shcmd);
    } else {
        // shell
        poclidek_setup(cctx);
        rc = poclidek_shell(cctx);
    }

    return rc;
}

static int run_ipoldek(struct poclidek_ctx *cctx)
{
    int rc = 1;
#if ENABLE_TRACE
    int i = 0;
    printf("run_ipoldek argc = %d\n", g_args.argc);
    while (g_args.argv[i])
        printf(" %s", g_args.argv[i++]);
    printf("\n");
#endif

    if (g_args.argc > 0) { // batch mode
        rc = poclidek_exec(cctx, g_args.ts, g_args.argc,
                           (const char **)g_args.argv);
    } else { /* lonely ipoldek -> run shell as poldek does */
        rc = run_poldek(cctx);
    }

    return rc;
}


int main(int argc, char **argv)
{
    struct poldek_ctx    *ctx;
    struct poclidek_ctx  *cctx;
    struct poldek_ts     *ts;
    int  rc = 1, rrc, mode = RUNMODE_POLDEK;
    const char *bn;

    if (!poldeklib_init())
        return 1;

    if (!do_su(argc, argv))
        return 1;

    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");

    bn = n_basenam(argv[0]);
    if (strcmp(bn, "apoldek") == 0 || strcmp(bn, "ipoldek") == 0) {
        mode = RUNMODE_APT;
    }
    /* else if (argc > 1 && *argv[1] != '-') {
        mode = RUNMODE_APT;
    }*/

    DBGF("mode %d %s %s\n", mode, n_basenam(argv[0]), argv[0]);

    ctx = poldek_new(0);

    ts = poldek_ts_new(ctx, 0);
    cctx = poclidek_new(ctx);

    parse_options(cctx, ts, argc, argv, mode);

    if (!poldek_setup(ctx))
        exit(EXIT_FAILURE);

    if (poldek_VERBOSE == 0)
        poldek_set_verbose(1);

    rrc = do_run();
    if (rrc & OPGROUP_RC_ERROR)
        exit(EXIT_FAILURE);

#define ENABLE_TRACE 0
#if ENABLE_TRACE
    int i = 0;
    printf("mode %d, argc = %d\n", g_args.mode, g_args.argc);
    while (g_args.argv[i])
        printf(" %s", g_args.argv[i++]);
    printf("\n");
#endif

    if ((g_args.cnflags & OPT_AS_FLAG(OPT_SHELL)) == 0) { /*no explicit --shell*/
        if (g_args.argc == 0 && (rrc & OPGROUP_RC_OK)) /* something minor cmd was executed  */
            goto out;

        if (g_args.cnflags & OPT_AS_FLAG(OPT_UPCONF)) /*UPCONF is major mode*/
            goto out;
    }

    //call moved down for lazy load
    //poclidek_setup(cctx);

    /* global --version is already parsed and handled here,
       disabling it here prevents argp from adding it to all commands */
    disable_version_opt_and_bug_address();

    if (g_args.mode == RUNMODE_POLDEK)
        rc = run_poldek(cctx);
    else if (g_args.mode == RUNMODE_APT)
        rc = run_ipoldek(cctx);

out:
    poldek_ts_free(ts);
    poclidek_free(cctx);
    poldek_free(ctx);
    poldeklib_destroy();

    return rc ? 0 : -1;
}


#if GENDOCBOOK
#define GENDOCBOOK_SECT  1        /* gen <sect2> */
static void docbook_opt(tn_hash *idh, FILE **st,
                        struct argp_option *opt, tn_array *aliases)
{
    FILE *stream = *st;
    char *id = NULL, docfile[PATH_MAX], *doc;
    int c;

    if (opt->doc && opt->name == NULL && opt->key == 0) { /* group */
        char *name, *p, path[PATH_MAX], doc[PATH_MAX], idsect[256];
        int n;

        if (stream) {
            fprintf(stream, "</variablelist>\n");
#if GENDOCBOOK_SECT
            fprintf(stream, "</sect2>\n");
#endif
            //printf("->CLOSE\n");
            fclose(stream);
        }

        n_strdupap(opt->doc, &name);
        p = name;
        while (*p) {
            if (isspace(*p)) *p = '_';
            if (*p == ':')   *p = '\0';
            if (*p == '/')   *p = '-';
            *p = tolower(*p);
            p++;

        }
        n_snprintf(idsect, sizeof(idsect), "ref.cmdl.%s", name);
        n_snprintf(path, sizeof(path), "manual/ref%.4d-%s.xml", opt->group, name);
        stream = fopen(path, "w");
        //printf("->OPEN %s\n", path);
        if (stream == NULL) {
            //printf("->OPEN %s\n", path);
            n_assert(stream);
        }
        fprintf(stream, "<!-- This file is autogenerated, DO NOT modify it -->\n\n");
        *st = stream;
        n = n_snprintf(doc, sizeof(doc), "%s", opt->doc);
        if (doc[n - 1] == ':')
            doc[n - 1] = '\0';
#if GENDOCBOOK_SECT
        fprintf(stream, "<sect2 id=\"%s\"><title>%s</title>\n", idsect, doc);
        fprintf(stream, "<variablelist><title></title>\n", doc);
#else
        fprintf(stream, "<variablelist><title>%s</title>\n", doc);
#endif

        return;
    }
    n_assert(stream);
    fprintf(stream, "<varlistentry><term><option");
    id = alloca(256);
    if (opt->name)
        strcpy(id, opt->name);
    else {
        c = opt->key;
        n_assert (c > 0 && c < 255 && isascii(c));
        n_snprintf(id, 256, "%c", c);
    }
    //printf("id = %s\n", id);
    if (!n_hash_exists(idh, id))
        n_hash_insert(idh, id, NULL);
    else {
       /* suffix with id_group, not so nice  */
        n_snprintf(&id[strlen(id)], 200, "%d", opt->group);
        n_hash_insert(idh, id, NULL);
    }

    fprintf(stream, " id=\"ref.cmdl.%s\"", id);
    fprintf(stream, ">");
    c = opt->key;
    if (c > 0 && c < 255 && isascii(c))
        fprintf(stream, "-%c", c);
    else
        c = 0;

    if (opt->name)
        fprintf(stream, "%s--%s", c ? ", " : "", opt->name);

    fprintf(stream, "%s%s%s%s",
            (opt->flags & OPTION_ARG_OPTIONAL) ? "[" : "",
            opt->arg ? "=" : "", opt->arg ? opt->arg : "",
            (opt->flags & OPTION_ARG_OPTIONAL) ? "]" : "");

    fprintf(stream, " </option></term>\n");
    fprintf(stream, "  <listitem>\n");
    n_snprintf(docfile, sizeof(docfile), "manual/ref.cmdl.%s.xml", id);
    doc = opt->doc ? (char*)opt->doc : "-";

    if (access(docfile, R_OK) != 0) {
        fprintf(stream, "   <para>\n      %s\n", doc);
        fprintf(stream, "   </para>\n");

    } else {
        fprintf(stream, "<xi:include  href=\"%s\" "
                "xmlns:xi=\"http://www.w3.org/2001/XInclude\" />", docfile);
    }


    fprintf(stream, "  </listitem>\n");
    fprintf(stream, "</varlistentry>\n");
}

static
void do_argp_as_docbook(struct argp *argp, FILE **stream, tn_hash *idh)
{
    struct argp *child;
    tn_array *aliases = n_array_new(16, NULL, NULL);
    int i;

    i = 0;
    while (1) {
        struct argp_option *next, *opt;

        n_array_clean(aliases);
        opt = &argp->options[i++];
        if (opt->name == NULL && opt->key == 0 && opt->doc == NULL)
            break;

        if (opt->flags & OPTION_HIDDEN) {
            printf(" skip %s\n", opt->name);
            continue;
        }
        //printf("do %d) %d %s %s\n", i + 1, opt->key, opt->name, opt->doc);

        next = &argp->options[i];
        while (next->flags & OPTION_ALIAS) {
            //printf("  %d) %d %s\n", i + 1, next->key, next->name);
            if ((next->flags & OPTION_HIDDEN) == 0)
                n_array_push(aliases, (void*)next);
            next = &argp->options[++i];
            if (next->name == NULL && next->key == 0 && next->doc == NULL)
                break;
        }
        //printf("stream %p\n", stream);
        docbook_opt(idh, stream, opt, aliases);

    }

    if (argp->children == NULL)
        return;

    i = 0;
    while ((child = argp->children[i++].argp))
        do_argp_as_docbook(child, stream, idh);

    n_array_free(aliases);
}

/* we must be in doc/ subdir */
static void argp_as_docbook(struct argp *argp)
{
    FILE *stream = NULL;
    tn_hash *idh = n_hash_new(256, free);
    do_argp_as_docbook(argp, &stream, idh);
    if (stream) {
        fprintf(stream, "</variablelist>\n");
#if GENDOCBOOK_SECT
        fprintf(stream, "</sect2>\n");
#endif
        fclose(stream);
    }
}
#endif  /* GENDOCBOOK */
