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
#include <fnmatch.h>

#include <trurl/narray.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include <vfile/vfile.h>
#include <sigint/sigint.h>

#include "i18n.h"
#include "log.h"
#include "pkgdir/source.h"
#include "pkgset.h"
#include "usrset.h"
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

struct mjrmode_conf mjrmodes[] = {
    { MODE_NULL,         NULL, 0 }, 
    { MODE_VERIFY,       "verify", MODE_F_LDSOURCES }, 
    { MODE_MKIDX,        "mkidx",  MODE_F_LDSOURCES }, 
    { MODE_INSTALLDIST,  "install-dist", MODE_F_LDSOURCES },
    { MODE_INSTALL,      "install", MODE_F_LDSOURCES }, 
    { MODE_UNINSTALL,    "uninstall", 0 }, 
    { MODE_SPLIT,        "split", MODE_F_LDSOURCES },
    { MODE_SRCLIST,      "sources", 0 }, 
    { -1, NULL,  0 }
};


static struct poclidek_opgroup *poclidek_opgroup_tab[] = {
    &poclidek_opgroup_source,
    &poclidek_opgroup_packages,
    &poclidek_opgroup_install,
    &poclidek_opgroup_uninstall,
    &poclidek_opgroup_makeidx,
    NULL
};


struct args {
    struct poldek_ctx    *ctx;
    struct poldekcli_ctx *cctx;
    struct poldek_ts     *ts;  
    int       mjrmode;
    unsigned  mnrmode;

    const char   *cmd;

    unsigned  cnflags;

    struct source *src_mkidx;

    int       has_pkgdef;
    tn_array  *pkgdef_files;    /* foo.rpm      */
    tn_array  *pkgdef_defs;     /* --nevr "foo 1.2" or "foo" or "foo*" */
    tn_array  *pkgdef_sets;     /* -p ftp://ftp.zenek.net/PLD/tiny */

    
    struct usrpkgset  *ups;
    
    char        *path_conf;
    char        *path_log;
    
    unsigned    pkgdir_creat_flags; 
    
    int         shell_skip_installed;
    char        *shcmd;

    tn_array    *opgroup_rts;

    int         argc;
    char        **argv;

} args;


void set_mjrmode(struct args *argsp, int mjrmode) 
{
    int i = 0;

    if (argsp->mjrmode != mjrmode) {
        logn(LOGERR, _("only one major mode may be specified"));
        exit(EXIT_FAILURE);
    }

    while (mjrmodes[i].no >= 0) {
        if (mjrmode == mjrmodes[i].no) {
            argsp->mjrmode = mjrmode;
            argsp->cmd = mjrmodes[i].cmd;
        }
    }
    n_assert(0);
}



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
            
        case 'q':
            verbose = -1;
            break;
            
        case 'v': 
            verbose++;
            break;

        case OPT_BANNER:
            printf("%s\n", poldek_BANNER);
            exit(EXIT_SUCCESS);
            break;
#if 0
        case ARGP_KEY_ARG:
            printf("arg[%d] = %s\n", argsp->argc, arg);
            argsp->argv[argsp->argc++] = arg;
            argsp->argv[argsp->argc] = NULL;
            break;
#endif
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}

// TODO: !O(n^2)
void hide_child_options(struct argp *parent, struct argp *child) 
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

void argp_prepare_child_options(struct argp *argp) 
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
void parse_options(struct poldekcli_ctx *cctx, int argc, char **argv) 
{
    struct argp argp = { common_options, parse_opt,
                         args_doc, poldek_BANNER, 0, 0, 0};
    struct argp_child *child;
    int n, i;


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
    
    argp_parse(&argp, argc, argv, 0, 0, &args);

    if ((args.cnflags & POLDEKCLI_CMN_NOCONF) == 0) 
        if (!poldek_load_config(args.ctx, args.path_conf))
            exit(EXIT_FAILURE);

    if (!poldek_setup(args.ctx))
        exit(EXIT_FAILURE);

    if (args.cnflags & POLDEKCLI_CMN_NOASK) 
        poldek_ts_setf(args.ctx->ts, POLDEK_TS_INTERACTIVE_ON);
    
    else if (poldek_ts_issetf(args.ctx->ts, POLDEK_TS_CONFIRM_INST) &&
             verbose < 1)
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
    
    for (i=0; i < n_array_size(args.opgroup_rts); i++) {
        int rc;
        struct poclidek_opgroup_rt *rt = n_array_nth(args.opgroup_rts, i);

        rc = 0;
        if (rt->run) {
            int rc;
            rc = rt->run(rt);
            
            if (rc & OPGROUP_RC_FINI)
                exit(EXIT_SUCCESS);
        }
    }
}


int main(int argc, char **argv)
{
    int                   i, rc = 1, load_dbdepdirs = 0;
    struct poldek_ctx     *ctx, poldek_ctx;
    struct poldekcli_ctx  cctx;
    char                  *cmd;
    
    mem_info_verbose = -1;

    ctx = &poldek_ctx;
    poldek_init(ctx, 0);

    memset(&cctx, 0, sizeof(cctx));
    poldekcli_init(&cctx, ctx, 1);

    parse_options(&cctx, argc, argv);

    //exit(0);
    //if (!poldek_load_config(ctx, NULL))
    //    exit(EXIT_FAILURE);

    //if (!poldek_setup(ctx))
    //    exit(EXIT_FAILURE);

    //
    //if (!poldekcli_load_packages(&cctx, 1))
    //    exit(EXIT_FAILURE);

    //printf("exec %d\n", args.argc);
    //if (args.argc > 0) 
    //    rc = poldekcli_exec(&cctx, args.argc, args.argv);

    poldekcli_destroy(&cctx);
    return rc;
}
