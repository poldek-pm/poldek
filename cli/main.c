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

#include <trurl/narray.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include <vfile/vfile.h>
#include <sigint/sigint.h>

#include "i18n.h"
#include "log.h"
#include "pkgdir/source.h"
#include "pkgset.h"
#include "misc.h"
#include "conf.h"
#include "split.h"
#include "poldek.h"
#include "cli.h"
#include "op.h"

#ifndef VERSION
# error "undefined VERSION"
#endif

static const char *argp_program_version = poldek_VERSION_BANNER;
const char *argp_program_bug_address = poldek_BANNER;
static char args_doc[] = N_("[PACKAGE...]");

#define OPT_ROOTDIR   'r'
#define OPT_CACHEDIR  1002
#define OPT_ASK       1003
#define OPT_NOASK     1004
#define OPT_CONF      1005
#define OPT_NOCONF    1006
#define OPT_BANNER    1007
#define OPT_LOG       1008

#define POLDEKCLI_CMN_NOCONF (1 << 0)
#define POLDEKCLI_CMN_NOASK  (1 << 1)

/* The options we understand. */
static struct argp_option common_options[] = {
{0,0,0,0, N_("Other:"), 10500 },
{"cmd", 'C', 0, 0, N_("Run in cmd mode"), 10500 },
{"ask", OPT_ASK, 0, 0, N_("Confirm packages installation and "
                          "let user choose among equivalent packages"), 10500 },
{"noask", OPT_NOASK, 0, 0, N_("Don't ask about anything"), 10500 }, 

{"conf", OPT_CONF, "FILE", 0, N_("Read configuration from FILE"), 10500 }, 
{"noconf", OPT_NOCONF, 0, 0, N_("Do not read configuration"), 10500 }, 

{"version", OPT_BANNER, 0, 0, N_("Display program version information and exit"),
     10500 },    
{"log", OPT_LOG, "FILE", 0, N_("Log program messages to FILE"), 10500 },
//{"v016", OPT_V016, 0, 0, N_("Read indexes created by versions < 0.17"), 500 },
{0,  'v', 0, 0, N_("Be verbose."), 10500 },
{0,  'q', 0, 0, N_("Do not produce any output."), 10500 },
{ 0, 0, 0, 0, 0, 0 },
};


#define MODE_NULL         0
#define MODE_VERIFY       1
#define MODE_MKIDX        2
#define MODE_INSTALL      4
#define MODE_INSTALLDIST  5
#define MODE_UNINSTALL    6
#define MODE_SPLIT        7
#define MODE_SRCLIST      8

#define MODE_F_LDSOURCES  (1 << 0)

struct mjrmode_conf {
    int         no;
    const char  *cmd;
    unsigned    flags;
};


static struct poclidek_opgroup *poclidek_opgroup_tab[] = {
    &poclidek_opgroup_source,
    &poclidek_opgroup_packages,
    &poclidek_opgroup_install,
    &poclidek_opgroup_uninstall,
    &poclidek_opgroup_makeidx,
    &poclidek_opgroup_split, 
    NULL
};


struct args {
    int                  eat_args;      
    struct poldek_ctx    *ctx;
    struct poclidek_ctx *cctx;
    struct poldek_ts     *ts;
    
    int       mjrmode;
    unsigned  mnrmode;

    unsigned  cnflags;

    char        *path_conf;
    char        *path_log;
    
    unsigned    pkgdir_creat_flags; 
    
    int         shell_skip_installed;
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
            poldek_configure(argsp->ctx, POLDEK_CONF_LOGFILE, arg);
            break;

        case OPT_CONF:
            args.path_conf = n_strdup(arg);
            break;
            
        case 'q':
            verbose = -1;
            break;

        case 'C':
            argsp->eat_args = 1;
            break;
            
        case 'v': 
            verbose++;
            break;

        case OPT_BANNER:
            printf("%s\n", poldek_BANNER);
            exit(EXIT_SUCCESS);
            break;

        case ARGP_KEY_ARG:
            if (argsp->eat_args) {
                argsp->argv[argsp->argc++] = arg;

                while (state->next < state->argc) {
                    char *arg = state->argv[state->next++];
                    argsp->argv[argsp->argc++] = arg;
                }
                argsp->argv[argsp->argc] = NULL;
                n_assert(state->next = state->argc);
                break;
            }
            /* no args->eat_args => no break */
            
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
void parse_options(struct poclidek_ctx *cctx, int argc, char **argv) 
{
    struct argp argp = { common_options, parse_opt,
                         args_doc, poldek_BANNER, 0, 0, 0};
    struct argp_child *child;
    int n, i, index, exit_program = 0, ec = EXIT_SUCCESS;


    memset(&args, 0, sizeof(args));
    args.argc = 0;
    args.argv = n_malloc(sizeof(*argv) * argc);
    args.argv[0] = NULL;

    args.ts = poldek_ts_new(cctx->ctx);
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
    
    verbose = 0;
    
    args.ctx = cctx->ctx;
    args.cctx = cctx;

    index = 0;
    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, &index, &args);

    poldek_setup_cachedir(args.ctx);

    if ((args.cnflags & POLDEKCLI_CMN_NOCONF) == 0) 
        if (!poldek_load_config(args.ctx, args.path_conf))
            exit(EXIT_FAILURE);

    if (!poldek_setup_sources(args.ctx))
        exit(EXIT_FAILURE);

    if ((args.cnflags & POLDEKCLI_CMN_NOASK) == 0) {
        args.ctx->ts->setop(args.ctx->ts, POLDEK_OP_CONFIRM_INST, 1);
        args.ctx->ts->setop(args.ctx->ts, POLDEK_OP_CONFIRM_UNINST, 1);
        args.ctx->ts->setop(args.ctx->ts, POLDEK_OP_EQPKG_ASKUSER, 1);
    }
    
    if (poldek_ts_is_interactive_on(args.ctx->ts) && verbose < 1)
        verbose = 1;
            
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
    int i, all_rc = 0, ec, exit_program = 0;
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
    
    if (exit_program) {
        printf("exit(%d)\n", ec);
        exit(ec);
    }
    
    return all_rc;
}


int main(int argc, char **argv)
{
    int                   i, rc = 1, rrc;
    struct poldek_ctx     ctx;
    struct poclidek_ctx  cctx;
    
    mem_info_verbose = -1;

    poldek_init(&ctx, 0);

    memset(&cctx, 0, sizeof(cctx));
    poclidek_init(&cctx, &ctx);
    
    parse_options(&cctx, argc, argv);
    
    rrc = do_run();
    if (rrc & OPGROUP_RC_FINI)
        exit((rc & OPGROUP_RC_ERROR) ? EXIT_FAILURE : EXIT_SUCCESS);
    
    if (args.eat_args == 0) {
        poclidek_load_packages(&cctx, 1);
        poclidek_shell(&cctx);
        
    } else {
        dbgf_("exec[%d] ", args.argc);
        i = 0;

        //while (args.argv[i])
        //    printf(" %s", args.argv[i++]);
        //printf("\n");
        
        if (args.argc > 0) 
            rc = poclidek_exec(&cctx, args.ts, args.argc,
                                (const char **)args.argv);
    }
    
    poclidek_destroy(&cctx);
    return rc;
}
