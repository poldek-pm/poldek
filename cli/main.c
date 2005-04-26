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

#define OPT_GID       1000

#define OPT_CACHEDIR  (OPT_GID + 1)
#define OPT_ASK       (OPT_GID + 2)
#define OPT_NOASK     (OPT_GID + 3)
#define OPT_CONF      (OPT_GID + 4)
#define OPT_CONFUP    (OPT_GID + 5)
#define OPT_NOCONF    (OPT_GID + 6)
#define OPT_BANNER    (OPT_GID + 7)
#define OPT_LOG       (OPT_GID + 8)
#define OPT_SKIPINSTALLED (OPT_GID + 9)
#define OPT_KEEPDOWNLOADS (OPT_GID + 10)
#define OPT_PM (OPT_GID + 11)
#define OPT_SHELL  (OPT_GID + 12)
#define OPT_SHELL_CMD (OPT_GID + 13)
#define OPT_RUNAS (OPT_GID + 14)
#define OPT_DOCB   (OPT_GID + 15)

#define OPT_AS_FLAG(OPT)       (1 << (OPT - OPT_GID))

/* The options we understand. */
static struct argp_option common_options[] = {
{0,0,0,0, N_("Other:"), OPT_GID },
{"pm", OPT_PM, "PM", OPTION_HIDDEN, 0, OPT_GID },
{"cachedir", OPT_CACHEDIR, "DIR", 0,
     N_("Store downloaded files & co. under DIR"), OPT_GID },
{"cmd", 'C', 0, 0, N_("Run in cmd mode"), OPT_GID },
{"ask", OPT_ASK, 0, 0, N_("Confirm packages installation and "
                          "let user choose among equivalent packages"), OPT_GID },
{"noask", OPT_NOASK, 0, 0, N_("Don't ask about anything"), OPT_GID },

{"shell", OPT_SHELL, 0, 0, N_("Run in interactive mode (default)"), OPT_GID },

{"shcmd", OPT_SHELL_CMD, "COMMAND", OPTION_HIDDEN,
                 N_("Run poldek shell COMMAND and exit"), OPT_GID },    

{"skip-installed", OPT_SKIPINSTALLED, 0, 0,
     N_("Don't load installed packages at startup"), OPT_GID },
{"fast", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, NULL, OPT_GID },
{ 0, 'f', 0, OPTION_ALIAS | OPTION_HIDDEN, NULL, OPT_GID },

{"conf", OPT_CONF, "FILE", 0, N_("Read configuration from FILE"), OPT_GID },
{"_conf", 'c', "FILE", OPTION_HIDDEN, N_("Read configuration from FILE"), OPT_GID }, 
{"noconf", OPT_NOCONF, 0, 0, N_("Do not read configuration"), OPT_GID },
{"upconf", OPT_CONFUP, 0, 0, N_("Update remote configuration files (if any)"), OPT_GID },

{"keep-downloads", OPT_KEEPDOWNLOADS, 0, 0,
N_("Do not remove downloaded packages just after their installation"), OPT_GID },
{"version", OPT_BANNER, 0, 0, N_("Display program version information and exit"),
     OPT_GID },    
{"log", OPT_LOG, "FILE", 0, N_("Log program messages to FILE"), OPT_GID },
{"runas", OPT_RUNAS, "USER", 0, N_("Run program as user USER"), OPT_GID },
{"docbook", OPT_DOCB, 0, OPTION_HIDDEN,
        N_("Dump options in docbook format"), OPT_GID },
//{"v016", OPT_V016, 0, 0, N_("Read indexes created by versions < 0.17"), 500 },
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
            argsp->cnflags |= OPT_AS_FLAG(OPT_CONFUP);
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
            argsp->cnflags |= OPT_AS_FLAG(OPT_NOCONF);

        case OPT_ASK:
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 1);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 1);
            break;

        case OPT_NOASK:
            argsp->cnflags |= OPT_AS_FLAG(OPT_NOASK);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_INST, 0);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_CONFIRM_UNINST, 0);
            poldek_configure(ctx, POLDEK_CONF_OPT, POLDEK_OP_EQPKG_ASKUSER, 0);
            break;

        case OPT_RUNAS:         /* ignored, catched at startup */
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
            argsp->cnflags |= OPT_AS_FLAG(OPT_SKIPINSTALLED);
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

        case OPT_DOCB:
            argsp->cnflags |= OPT_AS_FLAG(OPT_DOCB);
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
        DBGF("%d) %d %s\n", i + 1, opt->key, opt->name);

        key = opt->key;
        j = 0;
        while (1) {
            struct argp_option *opt = (struct argp_option *)&child->options[j++];
            if (opt->name == NULL && opt->key == 0 && opt->doc == NULL)
                break;

            DBGF("  %d. %d %s\n", j + 1, opt->key, opt->name);
            if (key && key == opt->key) {
                DBGF("Hide %d %s (%s)\n", opt->key, opt->name, opt->doc);
                opt->flags |= OPTION_HIDDEN;
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

#if GENDOCBOOK
static void docbook_opt(FILE **st,
                        struct argp_option *opt, tn_array *aliases)
{
    FILE *stream = *st;
    char *id = NULL;
    int c;
    
    if (opt->doc && opt->name == NULL && opt->key == 0) { /* group */
        char *name, *p, path[PATH_MAX];
        
        if (stream) {
            fprintf(stream, "</itemizedlist>\n");
            printf("->CLOSE\n");
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
        
        n_snprintf(path, sizeof(path), "ref-%s.xml", name);
        stream = fopen(path, "w");
        printf("->OPEN %s\n", path);
        if (stream == NULL) {
            printf("->OPEN %s\n", path);
            n_assert(stream);
        }
        
        *st = stream;
        fprintf(stream, "<itemizedlist><title>%s</title>\n", opt->doc);
        return;
    }
    n_assert(stream);
    fprintf(stream, "<listitem> <para><option");
    id = opt->name;
    if (id == NULL) {
        id = alloca(256);
        c = opt->key;
        n_assert (c > 0 && c < 255 && isascii(c));
        n_snprintf(id, 256, "%c", c);
    }
    printf("id = %s\n", id);
    
    fprintf(stream, " id=\"ref.%s\"", id);
    fprintf(stream, ">");
    c = opt->key;
    if (c > 0 && c < 255 && isascii(c))
        fprintf(stream, "-%c", c);
    else
        c = 0;

    if (opt->name)
        fprintf(stream, "%s--%s%s%s%s%s", c ? ", " : "", opt->name,
                (opt->flags & OPTION_ARG_OPTIONAL) ? "[" : "",
                opt->arg ? "=" : "", opt->arg ? opt->arg : "",
                (opt->flags & OPTION_ARG_OPTIONAL) ? "]" : "");
    
    fprintf(stream, " </option></para>\n");
    fprintf(stream, "  <para>\n    %s\n", opt->doc ? opt->doc : "NODOC");
    fprintf(stream, "  </para>\n");
    fprintf(stream, "</listitem>\n");
}

static
void do_argp_as_docbook(struct argp *argp, FILE **stream) 
{
    const struct argp *child;
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
        printf("do %d) %d %s %s\n", i + 1, opt->key, opt->name, opt->doc);
        
        next = &argp->options[i];
        while (next->flags & OPTION_ALIAS) {
            printf("  %d) %d %s\n", i + 1, next->key, next->name);
            if ((next->flags & OPTION_HIDDEN) == 0)
                n_array_push(aliases, (void*)next);
            next = &argp->options[++i];
            if (next->name == NULL && next->key == 0 && next->doc == NULL)
                break;
        }
        printf("stream %p\n", stream);
        docbook_opt(stream, opt, aliases);
        
    }

    if (argp->children == NULL)
        return;
    
    i = 0;
    while ((child = argp->children[i++].argp))
        do_argp_as_docbook(child, stream);

    n_array_free(aliases);
}

static
void argp_as_docbook(struct argp *argp) 
{
    FILE *stream = NULL;
    do_argp_as_docbook(argp, &stream);
    if (stream) {
        fprintf(stream, "</itemizedlist>\n");
        fclose(stream);
    }
}
#endif  /* GENDOCBOOK */

static
void parse_options(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                   int argc, char **argv, int mode) 
{
    struct argp argp = { common_options, parse_opt,
                         args_doc, poldek_BANNER, 0, 0, 0};
    int n, i, index, hide_child_opts = 0;
    struct argp_child *child;

    memset(&args, 0, sizeof(args));
    args.argc = 0;
    args.argv = n_malloc(sizeof(*argv) * argc);
    args.argv[0] = NULL;

    args.mode = mode;
    args.ts = ts;
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
    if (poclidek_argv_is_help(argc, (const char **)argv))
        hide_child_opts = 1;
    argp_prepare_child_options(&argp, hide_child_opts);
    
    poldek_set_verbose(0);
    
    args.ctx = cctx->ctx;
    args.cctx = cctx;

    index = 0;
    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, &index, &args);
    
#if GENDOCBOOK
    if (args.cnflags & OPT_AS_FLAG(OPT_DOCB)) {
        argp_as_docbook(&argp);
        exit(EXIT_SUCCESS);
    }
#endif
    
    if ((args.cnflags & OPT_AS_FLAG(OPT_NOCONF)) == 0) 
        if (!poldek_load_config(args.ctx, args.path_conf,
                                (args.cnflags & OPT_AS_FLAG(OPT_CONFUP)) ? 1 : 0))
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
        
        cnf = poldek_conf_loadefault(POLDEK_LDCONF_NOINCLUDE |
                                     POLDEK_LDCONF_FOREIGN);
        if (cnf) {
            tn_hash *global;
            const char *u;
            
            global = poldek_conf_get_section_ht(cnf, "global");
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

static int load_packages(struct poclidek_ctx *cctx, unsigned cnflags)
{
    unsigned ldflags = POCLIDEK_LOAD_AVAILABLE;
    
    if ((cnflags & OPT_AS_FLAG(OPT_SKIPINSTALLED)) == 0)
        ldflags |= POCLIDEK_LOAD_INSTALLED;
        
    if (!poclidek_load_packages(cctx, ldflags)) {
        logn(LOGERR, "packages load failed");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    struct poldek_ctx    *ctx;
    struct poclidek_ctx  *cctx;
    struct poldek_ts     *ts;
    int  ec = 0, rrc, mode = RUNMODE_POLDEK;
    const char *bn;

    if (!poldeklib_init())
        return 1;

    if (!do_su(argc, argv))
        return 1;
    
    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");

    bn = n_basenam(argv[0]);

    if (strcmp(bn, "apoldek") == 0 || strcmp(bn, "ipoldek") == 0)
        mode = RUNMODE_APT;
    
    DBGF("mode %d %s %s\n", mode, n_basenam(argv[0]), argv[0]);

    ctx = poldek_new(0);
    ts = poldek_ts_new(ctx, 0);
    cctx = poclidek_new(ctx);
    parse_options(cctx, ts, argc, argv, mode);
    
    rrc = do_run();
    if (rrc & OPGROUP_RC_FINI)
        exit((rrc & OPGROUP_RC_ERROR) ? EXIT_FAILURE : EXIT_SUCCESS);
    
    if (args.mode == RUNMODE_POLDEK) {
        if (!load_packages(cctx, args.cnflags)) {
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
        else {                  /* lonely ipoldek -> run shell as well */
            //msgn(0, _("Give me something to do."));
            //rc = 0;
            
            if (!load_packages(cctx, args.cnflags))  {
                logn(LOGERR, "packages load failed");
                rc = 0;
                
            } else {
                ec = poclidek_shell(cctx);
            }
        }
        
        if (!rc)
            ec = 1;
    }
    poldek_ts_free(ts);
    poclidek_free(cctx);
    poldek_free(ctx);
    poldeklib_destroy();
    return ec;
}
