
/* 
  Copyright (C) 2000 - 2003 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <signal.h>
#include <time.h>
#include <argp.h>
#include <fnmatch.h>
#include <time.h>

#include <pcre.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <trurl/trurl.h>

#include <sigint/sigint.h>
#include "i18n.h"
#include "misc.h"
#include "log.h"
#include "cli.h"
#include "conf.h"

int shOnTTY = 0;

static volatile sig_atomic_t shInCmd  = 0;

static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int argv_is_help(int argc, const char **argv);

extern struct command command_ls;
extern struct command command_install;
extern struct command command_uninstall;
//extern struct command command_get;
extern struct command command_search;
extern struct command command_desc;

static struct command *commands_tab[] = {
    &command_ls,
    &command_install, 
    &command_uninstall,
    &command_search,
    &command_desc, 
    NULL
};



struct sh_cmdarg {
    unsigned        cmdflags;
    int             err;
    struct cmdarg   *cmdarg;
    struct command  *cmd;   
    error_t (*parse_opt_fn)(int, char*, struct argp_state*);
};

/* default parse_opt */
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct sh_cmdarg *sh_cmdarg = state->input;
    unsigned flags = sh_cmdarg->cmdflags;
    int rc;


    state->input = sh_cmdarg->cmdarg;
    
    if (sh_cmdarg->parse_opt_fn)
        rc = sh_cmdarg->parse_opt_fn(key, arg, state);
    else
        rc = ARGP_ERR_UNKNOWN;
    state->input = sh_cmdarg;
    
    if (rc == EINVAL) 
        sh_cmdarg->err = 1;

    if (rc != ARGP_ERR_UNKNOWN)
        return rc;
    
    rc = 0;

    switch (key) {
        case 'v': {
            if ((flags & COMMAND_HASVERBOSE) == 0) {
                argp_usage (state);
                sh_cmdarg->err = 1;
                
            } else {
                verbose++;
            }
        }
        break;
        
            
        case ARGP_KEY_ARG:
            if (flags & COMMAND_NOARGS) {
                argp_usage (state);
                sh_cmdarg->err = 1; 
                return EINVAL;
            }
            //printf("push\n");
            n_array_push(sh_cmdarg->cmdarg->pkgnames, arg);
            break;
            
        case 'h':
            argp_state_help(state, stdout, ARGP_HELP_LONG | ARGP_HELP_DOC |
                            ARGP_HELP_USAGE);
            return EINVAL;
            break;
            
        case ARGP_KEY_NO_ARGS:
            if (sh_cmdarg->cmdarg->is_help)
                break;
            
            //printf("ARGP_KEY_NO_ARGS --\n");
            if ((flags & COMMAND_NOARGS) == 0 &&
                (flags & COMMAND_EMPTYARGS) == 0) {
                //printf("ARGP_KEY_NO_ARGS\n");
                argp_usage (state);
                sh_cmdarg->err = 1; 
                return EINVAL;
            }
            break;
            
            
        case ARGP_KEY_ERROR:
            //printf("ARGP_KEY_ERROR\n");
            sh_cmdarg->err = 1;
            return EINVAL;
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    //printf("key = %d, rc = %d\n", key, rc);
    return rc;
}

static char *help_filter(int key, const char *text, void *input) 
{
    struct sh_cmdarg *sh_cmdarg = input;

    if (key == ARGP_KEY_HELP_EXTRA) {
        char *p, buf[4096];
        int n = 0;
		struct command_alias alias;

        
        if (sh_cmdarg->cmd->extra_help) 
            n += n_snprintf(&buf[n], sizeof(buf) - n, "  %s\n",
                          sh_cmdarg->cmd->extra_help);

		alias.cmd = sh_cmdarg->cmd;
#if 0
        if (n_array_bsearch_ex(cctx->aliases, &alias,
                               (tn_fn_cmp)command_alias_cmd_cmp)) {
           int i = 0;
		   struct command_alias *alias;

            n += n_snprintf(&buf[n], sizeof(buf) - n, "%s",
                            _("  Defined aliases:\n"));
			
            while (i < n_array_size(cctx->aliases)) {
				alias = n_array_nth(cctx->aliases, i);
				if (alias->cmd == sh_cmdarg->cmd)
	                n += n_snprintf(&buf[n], sizeof(buf) - n,
                                    "    %-16s  \"%s\"\n",
                                    alias->name, alias->cmdline);
				i++;
            }
        }
#endif        
        if (n > 0) {
            p = n_malloc(n + 1);
            return memcpy(p, buf, n + 1);
        }
    }
    
    return (char*)text;
}

static
int docmd(struct poldekcli_ctx *cctx, struct command *cmd,
          int argc, const char **argv)
{
    struct cmdarg        cmdarg;
    struct sh_cmdarg     sh_cmdarg;
    int                  rc = 1, verbose_;
    unsigned             parse_flags;
    struct argp          argp = { cmd->argp_opts,
                                  parse_opt,
                                  cmd->arg, cmd->doc, 0, 0, 0};

    
    verbose_ = verbose;
    if (argv == NULL)
        return 0;

    cmdarg.is_help = argv_is_help(argc, argv);
    cmdarg.pkgnames = n_array_new(64, NULL, (tn_fn_cmp)strcmp);
    cmdarg.pkgs = NULL;
    cmdarg.flags = 0;
    cmdarg.cctx = cctx;
    //cmdarg.sh_s->inst->flags = cmdarg.sh_s->inst_flags_orig;
    cmdarg.d = NULL;
    
    if (cmd->init_cmd_arg_d)
        cmdarg.d = cmd->init_cmd_arg_d();

    if (cmd->cmd_fn) {
        rc = cmd->cmd_fn(&cmdarg, argc, argv, &argp);
        goto l_end;
    }

    
    parse_flags = argp_parse_flags;

    shInCmd = 1;
    
    if ((cmd->flags & COMMAND_NOHELP) &&
        (cmd->flags & COMMAND_NOARGS) &&
        (cmd->flags & COMMAND_NOOPTS)) {

        rc = cmd->do_cmd_fn(&cmdarg);
        goto l_end;
    }

    sh_cmdarg.cmdflags = cmd->flags; 
    sh_cmdarg.err = 0;
    sh_cmdarg.cmdarg = &cmdarg;
    sh_cmdarg.cmd = cmd;
    sh_cmdarg.parse_opt_fn = cmd->parse_opt_fn;

    argp.help_filter = help_filter;
    argp_parse(&argp, argc, (char**)argv, parse_flags, 0, (void*)&sh_cmdarg);

    if (sh_cmdarg.err) {
        rc = 0;
        goto l_end;
    }
    
    if (cmdarg.is_help) {
        rc = 1;
        goto l_end;
    }
    
    rc = cmd->do_cmd_fn(&cmdarg);

 l_end:
    shInCmd = 0;
    
    if (cmdarg.pkgnames)
        n_array_free(cmdarg.pkgnames);

    if (cmdarg.pkgs)
        n_array_free(cmdarg.pkgs);
    
    
    if (cmd->destroy_cmd_arg_d && cmdarg.d)
        cmd->destroy_cmd_arg_d(cmdarg.d);
#if 0                           /* DUPA */
    if ((cmd->flags & COMMAND_MODIFIESDB) && cmdarg.sh_s->ts_instpkgs > 0) {
        cmdarg.sh_s->dbpkgdir->ts = cmdarg.sh_s->ts_instpkgs;
        cmdarg.sh_s->ts_instpkgs = 0;
    }
#endif    
    verbose = verbose_;
    return rc;
}


static
int execute_line(struct poldekcli_ctx *cctx, char *line)
{
    struct command *cmd, tmpcmd;
	struct command_alias *alias, tmpalias;
    char *p;
    const char **args;
    int rc = 0;

    p = line;
    while (*p && !isspace(*p))
        p++;
    
    if (*p)
        *p = '\0';
    else
        p = NULL;
    
	tmpcmd.name = line;
	tmpalias.name = tmpcmd.name;
    
	if ((alias = n_array_bsearch(cctx->aliases, &tmpalias))) {
		char *l;
		int len;

        len = strlen(alias->cmdline) + 1;
		cmd = alias->cmd;

		if (p == NULL) {    /* no args */
			l = alloca(len);
			memcpy(l, alias->cmdline, len);
            
		} else {
			p++;
			len += strlen(p) + len + 1;
			l = alloca(len);
			snprintf(l, len, "%s %s", alias->cmdline, p);
			p = NULL;
		}
		//printf("alias exp %s -> %s\n", line, l);
		line = l;
        
	} else if ((cmd = n_array_bsearch(cctx->commands, &tmpcmd))) {
        ;                       /* do nothing */
        
    } else {
        logn(LOGERR, _("%s: no such command"), line);
        return 0;
    }

    if (p)
        *p = ' ';
    
    if ((args = n_str_tokl(line, " \t"))) {
        int argc = 0;

        while (args[argc])
            argc++;
        
        rc = docmd(cctx, cmd, argc, args);
        n_str_tokl_free(args);
    }
    
    return rc;
}


static
char *stripwhite(char *string)
{
    register char *s, *t;

    for (s = string; whitespace(*s); s++);

    if (*s == 0)
	return (s);

    t = s + strlen(s) - 1;
    while (t > s && whitespace(*t))
	t--;
    *++t = '\0';

    return s;
}



void sh_resolve_packages(tn_array *pkgnames, tn_array *avpkgs, tn_array **pkgsp,
                         int strict)
{
    tn_array *pkgs = NULL;
    int i, j;
    int *matches, *matches_bycmp;
    
    
    *pkgsp = NULL;

    for (i=0; i<n_array_size(pkgnames); i++) {
        char *name = n_array_nth(pkgnames, i);
        
        if (*name == '*' && *(name + 1) == '\0') {
            *pkgsp = avpkgs;
            return;
        }
    }

    matches = alloca(n_array_size(pkgnames) * sizeof(*matches));
    memset(matches, 0, n_array_size(pkgnames) * sizeof(*matches));

    matches_bycmp = alloca(n_array_size(pkgnames) * sizeof(*matches_bycmp));
    memset(matches_bycmp, 0, n_array_size(pkgnames) * sizeof(*matches_bycmp));
    
    
    pkgs = n_array_new(16, NULL, (tn_fn_cmp)pkg_nvr_strcmp);

    for (i=0; i<n_array_size(avpkgs); i++) {
        struct pkg *pkg = n_array_nth(avpkgs, i);
        
        for (j=0; j < n_array_size(pkgnames); j++) {
            char *mask = n_array_nth(pkgnames, j);

            if (strcmp(mask, pkg->name) == 0) {
                n_array_push(pkgs, pkg);
                matches_bycmp[j]++;
                matches[j]++;
                
            } else if (fnmatch(mask, pkg->nvr, 0) == 0) {
                n_array_push(pkgs, pkg);
                matches[j]++;
            }
        }
    }

    
    for (j=0; j < n_array_size(pkgnames); j++) {
        const char *mask = n_array_nth(pkgnames, j);
        
        if (matches[j] == 0) {
            logn(LOGERR, _("%s: no such package"), mask);
            if (strict && n_array_size(pkgs))
                n_array_clean(pkgs);
        }

        if (matches_bycmp[j] > 1) {
            int pri = strict ? LOGERR : LOGWARN;
            logn(pri, _("%s: ambiguous name"), mask);
            if (strict && n_array_size(pkgs))
                n_array_clean(pkgs);
        }
    }
    
    if (n_array_size(pkgs) > 0) {
        n_array_sort(pkgs);
        n_array_uniq(pkgs);
    }

    *pkgsp = pkgs;
}




/* argp workaround */
static int argv_is_help(int argc, const char **argv)
{
    int i, is_help = 0;

    for (i=0; i<argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-?") == 0 ||
            strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--usage") == 0) {
            
            is_help = 1;
            break;
        }
    }
    return is_help;
}

static
int cmd_help(struct cmdarg *cmdarg)
{
    int i = 0;
    
    cmdarg = cmdarg;
    
    //printf("%s\n", poldek_banner);
    while (commands_tab[i]) {
        struct command *cmd = commands_tab[i++];
        char buf[256], *p;
        
        
        p = cmd->arg ? cmd->arg : "";
        if (cmd->argp_opts) {
            snprintf(buf, sizeof(buf), _("[OPTION...] %s"), cmd->arg);
            p = buf;
        }
        printf("%-9s %-36s %s\n", cmd->name, p, cmd->doc);
    }
    printf(_("\nFor now \"search\" and \"desc\" commands don't work with "
             "installed packages.\n"));
    
    printf(_("\nType COMMAND -? for details.\n"));
    return 0;
}

int pkg_cmp_lookup(struct pkg *lpkg, tn_array *pkgs,
                   int compare_ver, int *cmprc,
                   char *evr, size_t size) 
{
    struct pkg *pkg = NULL;
    char name[256];
    int n, finded = 0;

    snprintf(name, sizeof(name), "%s-", lpkg->name);
    n = n_array_bsearch_idx_ex(pkgs, name, (tn_fn_cmp)pkg_nvr_strncmp);

    if (n == -1)
        return 0;

    while (n < n_array_size(pkgs)) {
        pkg = n_array_nth(pkgs, n++);

        if (strcmp(pkg->name, lpkg->name) == 0) {
            finded = 1;
            break;
        }

        if (*pkg->name != *lpkg->name)
            break;
    }
    
    if (!finded)
        return 0;
    
    if (compare_ver == 0)
        *cmprc = pkg_cmp_evr(lpkg, pkg);
    else 
        *cmprc = pkg_cmp_ver(lpkg, pkg);
    
    snprintf(evr, size, "%s-%s", pkg->ver, pkg->rel);
    
    return finded;
}

int sh_printf_c(FILE *stream, int color, const char *fmt, ...)
{
    va_list args;
    int n = 0;

    va_start(args, fmt);
    if (stream == stdout)
        n = vprintf_c(color, fmt, args);
    else
        n = vfprintf(stream, fmt, args);
    
    va_end(args);
    return n;
}

static
int command_cmp(struct command *c1, struct command *c2) 
{
    return strcmp(c1->name, c2->name);
}


static void init_commands(struct poldekcli_ctx *cctx) 
{
    int n = 0;
	char   *homedir;
    
    cctx->commands = n_array_new(16, NULL, (tn_fn_cmp)command_cmp);
    cctx->all_commands = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
	
    while (commands_tab[n] != NULL) {
        struct command *cmd = commands_tab[n++];
        if (cmd->argp_opts)
            translate_argp_options(cmd->argp_opts);

        cmd->arg = _(cmd->arg);
        cmd->doc = _(cmd->doc);

        n_array_push(cctx->commands, cmd);
        if (n_array_bsearch(cctx->all_commands, cmd->name)) {
            logn(LOGERR, _("ambiguous command %s"), cmd->name);
            exit(EXIT_FAILURE);
        }
        n_array_push(cctx->all_commands, cmd->name);
        n_array_sort(cctx->all_commands);
        
    }

	n_array_sort(cctx->commands);

    poldekcli_load_aliases(cctx, "/etc/poldek.alias");
	if ((homedir = getenv("HOME")) != NULL) {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/.poldek.alias", homedir);	
		poldekcli_load_aliases(cctx, path);
	}
}

int poldekcli_init(struct poldekcli_ctx *cctx, struct poldek_ctx *ctx,
                   int skip_installed) 
{
    int i;

    n_assert (cctx->ctx == NULL);
    cctx->flags = 0;
    cctx->ctx = ctx;
    cctx->avpkgs = NULL;
    cctx->instpkgs = NULL;


    cctx->arg_packages = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    cctx->arg_package_sets = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    
    init_commands(cctx);
    return 1;
}


void poldekcli_destroy(struct poldekcli_ctx *cctx) 
{
    if (cctx->avpkgs)
        n_array_free(cctx->avpkgs);
    
    if (cctx->instpkgs)
        n_array_free(cctx->instpkgs);

    memset(cctx, 0, sizeof(*cctx));
}


int poldekcli_load_packages(struct poldekcli_ctx *cctx, int skip_installed) 
{
    struct poldek_ctx *ctx;
    int load_dbdepdirs = 1;
    

    if (cctx->flags & POLDEKCLI_PACKAGES_LOADED)
        return 1;

    cctx->flags |= POLDEKCLI_PACKAGES_LOADED;

    ctx = cctx->ctx;
    
    if (!poldek_load_sources(ctx, 1))
        return 0;

    if (ctx->pkgs) {
        int i;

        cctx->avpkgs = n_array_new(n_array_size(ctx->pkgs),
                                   (tn_fn_free)pkg_free,
                                   (tn_fn_cmp)pkg_nvr_strcmp);
    
        for (i=0; i < n_array_size(ctx->pkgs); i++) {
            struct pkg *pkg = n_array_nth(ctx->pkgs, i);
            n_array_push(cctx->avpkgs, pkg_link(pkg));
        }

        n_array_ctl(cctx->avpkgs, TN_ARRAY_AUTOSORTED);
        n_array_sort(cctx->avpkgs);
    }
    
    
    cctx->instpkgs = NULL;
    if (skip_installed == 0) {
        cctx->instpkgs = n_array_new(1024, (tn_fn_free)pkg_free,
                                       (tn_fn_cmp)pkg_nvr_strcmp);
        n_array_ctl(cctx->instpkgs, TN_ARRAY_AUTOSORTED);
        //load_installed_packages(&shell_s, 0);
    }
    
    return 1;
}


int poldekcli_exec_line(struct poldekcli_ctx *cctx, const char *cmd) 
{
    char *s, *p;
    int rc = 0;
    
    p = alloca(strlen(cmd) + 1);
    memcpy(p, cmd, strlen(cmd) + 1);
    
    s = stripwhite(p);
    if (*s) 
        rc = execute_line(cctx, s);

    return rc;
}

#if 0
int find_alias() 
{
    
	if ((alias = n_array_bsearch(cctx->aliases, &tmpalias))) {
		char *l;
		int len;

        len = strlen(alias->cmdline) + 1;
		cmd = alias->cmd;

		if (p == NULL) {    /* no args */
			l = alloca(len);
			memcpy(l, alias->cmdline, len);
            
		} else {
			p++;
			len += strlen(p) + len + 1;
			l = alloca(len);
			snprintf(l, len, "%s %s", alias->cmdline, p);
			p = NULL;
		}
		//printf("alias exp %s -> %s\n", line, l);
		line = l;

#endif

        
int poldekcli_exec(struct poldekcli_ctx *cctx, int argc, char **argv)
{
    struct command *cmd, tmpcmd;
    int   rc = 0;

    printf("poldekcli_exec %d %d\n", n_array_size(cctx->commands), argc);
    n_array_sort(cctx->commands);
	tmpcmd.name = argv[0];
    if ((cmd = n_array_bsearch(cctx->commands, &tmpcmd))) {
        ;                       /* do nothing */
        
    } else {
        logn(LOGERR, _("%s: no such command"), tmpcmd.name);
        return 0;
    }

    return docmd(cctx, cmd, argc, argv);
}
 
 
