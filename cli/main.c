/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@pld.org.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
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

#include <stdlib.h>
#include <string.h>

#include <argp.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <time.h>

#include <trurl/trurl.h>

#include <vfile/vfile.h>
#include <sigint/sigint.h>

#include "i18n.h"
#include "log.h"
#include "pkgdir/source.h"
#include "conf.h"
#include "split.h"
#include "poldek.h"
#include "cli.h"
#include "op.h"

#ifndef VERSION
# error "undefined VERSION"
#endif

extern int poclidek_shell(struct poclidek_ctx *cctx);

static const char *argp_program_version = poldek_VERSION_BANNER;
const char *argp_program_bug_address = poldek_BUG_MAILADDR;
static char args_doc[] = N_("[PACKAGE...]");


#define OPT_CACHEDIR  1002
#define OPT_ASK       1003
#define OPT_NOASK     1004
#define OPT_CONF      1005
#define OPT_CONFUP    1006
#define OPT_NOCONF    1007
#define OPT_BANNER    1008
#define OPT_LOG       1009
#define OPT_SKIPINSTALLED 1010
#define OPT_KEEPDOWNLOADS 1013
#define OPT_PM 1015
#define OPT_SHELL  1020
#define OPT_SHELL_CMD 1025


#define OPT_CMN_NOCONF         (1 << 0)
#define OPT_CMN_NOASK          (1 << 1)
#define OPT_CMN_SKIPINSTALLED  (1 << 2)
#define OPT_CMN_CONFUP         (1 << 3)

/* The options we understand. */
static struct argp_option common_options[] = {
{0,0,0,0, N_("Other:"), 10500 },
{"pm", OPT_PM, "PM", OPTION_HIDDEN, 0, 10500 },
{"cachedir", OPT_CACHEDIR, "DIR", 0,
     N_("Store downloaded files & co. under DIR"), 10500 },
{"cmd", 'C', 0, 0, N_("Run in cmd mode"), 10500 },
{"ask", OPT_ASK, 0, 0, N_("Confirm packages installation and "
                          "let user choose among equivalent packages"), 10500 },
{"noask", OPT_NOASK, 0, 0, N_("Don't ask about anything"), 10500 },

{"shell", OPT_SHELL, 0, 0, N_("Run in interactive mode (default)"), 10500 },

{"shcmd", OPT_SHELL_CMD, "COMMAND", OPTION_HIDDEN,
                 N_("Run poldek shell COMMAND and exit"), 10500 },    

{"skip-installed", OPT_SKIPINSTALLED, 0, 0,
     N_("Don't load installed packages at startup"), 10500 },
{"fast", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, NULL, 10500 },
{ 0, 'f', 0, OPTION_ALIAS | OPTION_HIDDEN, NULL, 10500 },

{"conf", OPT_CONF, "FILE", 0, N_("Read configuration from FILE"), 10500 },
{"_conf", 'c', "FILE", OPTION_HIDDEN, N_("Read configuration from FILE"), 10500 }, 
{"noconf", OPT_NOCONF, 0, 0, N_("Do not read configuration"), 10500 },
{"upconf", OPT_CONFUP, 0, 0, N_("Update remote configuration files (if any)"), 10500 },

{"keep-downloads", OPT_KEEPDOWNLOADS, 0, 0,
N_("Do not remove downloaded packages just after their installation"), 10500 },
{"version", OPT_BANNER, 0, 0, N_("Display program version information and exit"),
     10500 },    
{"log", OPT_LOG, "FILE", 0, N_("Log program messages to FILE"), 10500 },
//{"v016", OPT_V016, 0, 0, N_("Read indexes created by versions < 0.17"), 500 },
{0,  'v', 0, 0, N_("Be verbose."), 10500 },
{0,  'q', 0, 0, N_("Do not produce any output."), 10500 },
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
#define MODE_SPLIT        7
#define MODE_SRCLIST      8
#define MODE_SHELL        9 

#define MODE_F_LDSOURCES  (1 << 0)


static struct poclidek_opgroup *poclidek_opgroup_tab[] = {
    &poclidek_opgroup_source,
    &poclidek_opgroup_packages,
    &poclidek_opgroup_install,
    &poclidek_opgroup_uninstall,
    &poclidek_opgroup_makeidx,
    &poclidek_opgroup_split,
    &poclidek_opgroup_verify, 
    NULL
};


struct args {
    struct poldek_ctx    *ctx;
    struct poclidek_ctx *cctx;
    struct poldek_ts     *ts;

    int       mode;
    int       mjrmode;
    unsigned  mnrmode;

    unsigned  cnflags;

    char        *path_conf;
    char        *path_log;
    
    unsigned    pkgdir_creat_flags; 
    
    char        *shcmd;

    tn_array    *opgroup_rts;

    int         argc;
    char        **argv;

} args;



/* Parse a single option. */
static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct args *argsp = state->input;
    struct poldek_ctx *ctx = argsp->ctx;
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
            poldek_setup_cachedir(argsp->ctx);
            break;

        case 'c':
            logn(LOGNOTICE, _("-c is depreciated, use --conf instead"));
                                /* no break */
        case OPT_CONF:
            args.path_conf = n_strdup(arg);
            break;

        case OPT_CONFUP:
            argsp->cnflags |= OPT_CMN_CONFUP;
            break;
            
        case 'q':
            poldek_set_verbose(-1);
            break;

        case 'C':
            argsp->mode = RUNMODE_APT;
            break;
            
        case 'v': 
            poldek_VERBOSE++;
            break;

        case OPT_NOCONF:
            argsp->cnflags |= OPT_CMN_NOCONF;

        case OPT_ASK:
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 1);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 1);
            break;

        case OPT_NOASK:
            argsp->cnflags |= OPT_CMN_NOASK;
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 0);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 0);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_EQPKG_ASKUSER, 0);
            break;

        case OPT_SHELL:         /* default */
            argsp->mjrmode = MODE_SHELL;
            break;
            
        case OPT_SHELL_CMD:
            argsp->shcmd = arg;
            argsp->mjrmode = MODE_SHELL;
            break;

        case 'f':
            logn(LOGWARN, "-f is obsoleted, use --skip-installed instead");
                                /* no break */
        case OPT_SKIPINSTALLED:
            argsp->cnflags |= OPT_CMN_SKIPINSTALLED;
            argsp->cctx->flags |= POCLIDEK_SKIP_INSTALLED;
            break;

        case OPT_KEEPDOWNLOADS:
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_KEEP_DOWNLOADS, 1);
            break;

        case OPT_PM:
            poldek_configure(ctx, POLDEK_CONF_PM, arg);
            break;            

        case OPT_BANNER:
            msgn(-1, "%s", poldek_BANNER);
            exit(EXIT_SUCCESS);
            break;

        case ARGP_KEY_ARG:
            DBGF("main.arg %s\n", arg);
            if (argsp->mode == RUNMODE_APT) {
                argsp->argv[argsp->argc++] = arg;

                while (state->next < state->argc) {
                    char *arg = state->argv[state->next++];
                    argsp->argv[argsp->argc++] = arg;
                }
                argsp->argv[argsp->argc] = NULL;
                n_assert(state->next = state->argc);
                break;
            }
            /* apt => no break */
            
        default:
            return ARGP_ERR_UNKNOWN;
            break;
    }
    
    return 0;
}

static
// TODO: !O(n^2)
void hide_child_options(const struct argp *parent, const struct argp *child) 
{
    int i = 0;
    
    while (1) {
        int j, key;
        const struct argp_option *opt = &parent->options[i++];
        if (opt->name == NULL && opt->key == 0 && opt->doc == NULL)
            break;

        key = opt->key;
        j = 0;
        while (1) {
            struct argp_option *opt = (struct argp_option *)&child->options[j++];
            if (opt->name == NULL && opt->key == 0 && opt->doc == NULL)
                break;
            
            if (key && key == opt->key)
                opt->flags |= OPTION_HIDDEN;
        }
    }
}

static
void argp_prepare_child_options(const struct argp *argp) 
{
    int i;
    const struct argp *child;
        
    if (argp->children == NULL)
        return;

    i = 0;
    while ((child = argp->children[i++].argp)) {
        hide_child_options(argp, child);
        argp_prepare_child_options(child);
    }
}


    
static
void parse_options(struct poclidek_ctx *cctx, int argc, char **argv, int mode) 
{
    struct argp argp = { common_options, parse_opt,
                         args_doc, poldek_BANNER, 0, 0, 0};
    int n, i, index;
    struct argp_child *child;

    memset(&args, 0, sizeof(args));
    args.argc = 0;
    args.argv = n_malloc(sizeof(*argv) * argc);
    args.argv[0] = NULL;

    args.mode = mode;
    
    args.ts = poldek_ts_new(cctx->ctx, 0);
    n = 0;
    while (poclidek_opgroup_tab[n])
        n++;

    child = alloca((n + 2) * sizeof(*child));
    args.opgroup_rts = n_array_new(n, NULL, NULL);
    
    for (i=0; i < n; i++) {
        struct poclidek_opgroup_rt *rt;
        child[i] = *(poclidek_opgroup_tab[i]->argp_child);
        rt = poclidek_opgroup_rt_new(args.ts);
        rt->run = poclidek_opgroup_tab[i]->run;
        n_array_push(args.opgroup_rts, rt);
    }
    child[i].argp = NULL;
    argp.children = child;
    
    argp_prepare_child_options(&argp);
    
    poldek_set_verbose(0);
    
    args.ctx = cctx->ctx;
    args.cctx = cctx;

    index = 0;
    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, &index, &args);

    if ((args.cnflags & OPT_CMN_NOCONF) == 0) 
        if (!poldek_load_config(args.ctx, args.path_conf,
                                (args.cnflags & OPT_CMN_CONFUP) ? 1 : 0))
            exit(EXIT_FAILURE);

    if (!poldek_setup(args.ctx))
        exit(EXIT_FAILURE);

    if (poldek_is_interactive_on(args.ctx) && poldek_VERBOSE == 0)
        poldek_VERBOSE = 1;
            
#if 0

    if (args.mjrmode == MODE_NULL && args.mnrmode == MODE_NULL) {
#ifdef ENABLE_INTERACTIVE_MODE
        args.mjrmode = MODE_SHELL;
#else         
        logn(LOGERR, _("so what?"));
        exit(EXIT_FAILURE);
#endif        
    }
#endif
    return;
}


int do_run(void) 
{
    int i, all_rc = 0, ec = EXIT_SUCCESS, exit_program = 0;
    n_assert(args.opgroup_rts);
    
    for (i=0; i < n_array_size(args.opgroup_rts); i++) {
        struct poclidek_opgroup_rt *rt = n_array_nth(args.opgroup_rts, i);
        int rc;

        if (rt->run == NULL)
            continue;
        
        rc = rt->run(rt);

        if (rc != OPGROUP_RC_NIL)
            all_rc |= OPGROUP_RC_FINI;

        if (rc & OPGROUP_RC_ERROR) {
            ec = EXIT_FAILURE;
            exit_program = 1;
        }
        
        if (rc & OPGROUP_RC_IFINI)
            exit(ec);
            
        if (rc & OPGROUP_RC_FINI)
            exit_program = 1;
    }

    n_array_free(args.opgroup_rts);
    args.opgroup_rts = NULL;
    
    if (exit_program)
        exit(ec);
    
    return all_rc;
}


int main(int argc, char **argv)
{
    struct poldek_ctx    *ctx;
    struct poclidek_ctx  *cctx;
    int  ec = 0, rrc, mode = RUNMODE_POLDEK;
    const char *bn;
    
    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");

    bn = n_basenam(argv[0]);

    if (strcmp(bn, "apoldek") == 0 || strcmp(bn, "ipoldek") == 0)
        mode = RUNMODE_APT;
    
    DBGF("mode %d %s %s\n", mode, n_basenam(argv[0]), argv[0]);
    ctx = poldek_new(0);
    cctx = poclidek_new(ctx);
    parse_options(cctx, argc, argv, mode);
    
    rrc = do_run();
    if (rrc & OPGROUP_RC_FINI)
        exit((rrc & OPGROUP_RC_ERROR) ? EXIT_FAILURE : EXIT_SUCCESS);
    
    if (args.mode == RUNMODE_POLDEK) {
        unsigned ldflags = POCLIDEK_LOAD_AVAILABLE;
        if ((args.cnflags & OPT_CMN_SKIPINSTALLED) == 0)
            ldflags |= POCLIDEK_LOAD_INSTALLED;
        
        if (!poclidek_load_packages(cctx, ldflags)) {
            logn(LOGERR, "packages load failed");
            ec = 1;
            
        } else {
            if (args.shcmd) 
                ec = poclidek_execline(cctx, args.ts, args.shcmd);
            else
                ec = poclidek_shell(cctx);

            ec = !ec;
        }
        
    } else {                    /* RUNMODE_APT */
        int rc = 1;
#if ENABLE_TRACE
        i = 0;
        DBGF("verbose %d, argc = %d\n", verbose, args.argc);
        while (args.argv[i])
            printf(" %s", args.argv[i++]);
        printf("\n");
#endif        
        if (args.argc > 0)
            rc = poclidek_exec(cctx, args.ts, args.argc,
                               (const char **)args.argv);
        else {
            msgn(0, _("Give me something to do."));
            rc = 0;
        }
        
        if (!rc)
            ec = 1;
    }
    poclidek_free(cctx);
    poldek_free(ctx);
    return ec;
}
