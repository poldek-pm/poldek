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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <signal.h>
#include <argp.h>
#include <fnmatch.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <trurl/trurl.h>

#include "compiler.h"
#include "sigint/sigint.h"
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "capreq.h"
#include "pkg.h"
#include "poldek_term.h"
#include "cmd.h"
#define POCLIDEK_ITSELF
#include "poclidek.h"

static int cmd_quit(struct cmdctx *cmdctx);
struct poclidek_cmd command_quit = {
    COMMAND_NOARGS | COMMAND_NOOPTS, 
    "quit", NULL, N_("Exit poldek"), 
    NULL, NULL, NULL, cmd_quit,
    NULL, NULL, NULL, NULL, NULL, 0, 0
};

static volatile sig_atomic_t shDone   = 0;
static volatile sig_atomic_t shInCmd  = 0;
static int shQuit = 0;          /* cmd_quit */
static char *histfile;


#define COMPLETITION_CTX_NONE            0 /* current directory */
#define COMPLETITION_CTX_AVAILABLE       1 /* /all-avail */
#define COMPLETITION_CTX_UPGRADEABLE     2 
#define COMPLETITION_CTX_INSTALLED       3
#define COMPLETITION_CTX_WHAT_PROVIDES   4
#define COMPLETITION_CTX_WHAT_REQUIRES   5
#define COMPLETITION_CTX_WHAT_SUGGESTS   6
#define COMPLETITION_CTX_DIRNAME         7

struct sh_ctx {
    int completion_ctx;
    struct poclidek_ctx  *cctx;
};

static struct sh_ctx sh_ctx = { COMPLETITION_CTX_NONE, NULL };

inline static int option_is_end (const struct argp_option *__opt)
{
    return !__opt->key && !__opt->name && !__opt->doc && !__opt->group;
}

static
int is_upgradeable(struct poclidek_ctx *cctx, struct pkg *pkg, int reverse)
{
    struct pkg *ipkg = NULL;
    tn_array *dents;
    char name[256];
    int n, name_len;

    if (reverse)
        dents = poclidek_get_dent_ents(cctx, POCLIDEK_AVAILDIR);
    else
        dents = poclidek_get_dent_ents(cctx, POCLIDEK_INSTALLEDDIR);
    
    if (dents == NULL)
        return 1;
    
    name_len = snprintf(name, sizeof(name), "%s-", pkg->name);
    n = n_array_bsearch_idx_ex(dents, name, (tn_fn_cmp)pkg_dent_strncmp);

    if (n == -1)
        return 0;

    while (n < n_array_size(dents)) {
        struct pkg_dent *ent = n_array_nth(dents, n++);
        int cmprc;
        
        if (pkg_dent_isdir(ent))
            continue;

        if (strncmp(name, ent->name, name_len) != 0) 
            break;
        
        ipkg = ent->pkg_dent_pkg;
        if (!pkg_is_kind_of(ipkg, pkg))
            continue;

        if ((cmprc = pkg_cmp_evr(pkg, ipkg)) != 0) {
            DBGF("%s %s %d (%d)\n", pkg_id(pkg), pkg_id(ipkg), cmprc, reverse);
            
            if (!reverse && cmprc > 0)
                return 1;

            if (reverse && cmprc < 0)
                return 1;
        }
    }

    return 0;
}

static char *command_generator(const char *text, int state)
{
    static int i, len;
    char *name = NULL;
    struct poclidek_cmd tmpcmd;

	tmpcmd.name = (char*)text;
    if (state == 0) {
        len = strlen(text);
        if (len == 0)
            i = 0;
        else 
            i = n_array_bsearch_idx_ex(sh_ctx.cctx->commands, &tmpcmd,
                                       (tn_fn_cmp)poclidek_cmd_ncmp);
    }

    if (i > -1 && i < n_array_size(sh_ctx.cctx->commands)) {
        struct poclidek_cmd *cmd = n_array_nth(sh_ctx.cctx->commands, i++);
        if (len == 0 || strncmp(cmd->name, text, len) == 0) 
            name = n_strdup(cmd->name);
    }
    
    return name;
}

static char *command_options_generator(const char *text, int state)
{
    static tn_array *opts_table = NULL;
    
    if (state == 0) {
        struct poclidek_cmd *command = NULL;
        char *p, *e = NULL, line[64];
        int i, len;
        
        p = rl_line_buffer;

        if ((e = strchr(p, ' ')) == NULL)
            return NULL;
        
        n_assert(e - p + 1 <= 64);
        
        n_strncpy(line, p, e - p + 1);
    
        for (i = 0; i < n_array_size(sh_ctx.cctx->commands); i++) {
            struct poclidek_cmd *cmd = n_array_nth(sh_ctx.cctx->commands, i);
        
            if (n_str_eq(cmd->name, line)) {
                if (cmd->aliasto) {
                    struct poclidek_cmd tmpcmd;
                
                    tmpcmd.name = cmd->aliasto;
                    command = n_array_bsearch(sh_ctx.cctx->commands, &tmpcmd);

                } else {
                    command = cmd;
                }
            
                break;
            }
        }
        
        if (command == NULL)
            return NULL;
    
        opts_table = n_array_new(4, NULL, (tn_fn_cmp)strcmp);
    
        len = strlen(&text[2]);
    
        for (i = 0; !option_is_end(&command->argp_opts[i]); i++) {
            const struct argp_option *argp_opt = &command->argp_opts[i];
        
            /* skip hidden options */
            if (argp_opt->flags & OPTION_HIDDEN)
                continue;
        
            if (argp_opt->name && strncmp(argp_opt->name, &text[2], len) == 0) {
                n_array_push(opts_table, (void *) argp_opt->name);
            }
        }
    
        n_array_sort(opts_table);
    }
    
    if (state >= n_array_size(opts_table)) {
        n_array_cfree(&opts_table);
        return NULL;
    }
    
    return n_str_concat("--", n_array_nth(opts_table, state), NULL);
}

static char *arg_generator(const char *text, int state, int genpackages)
{
    int                  uprev = 0, upgradeable_mode = 0;
    static int           i, len;
    const char           *name = NULL;
    tn_array             *ents;

    if (sh_ctx.completion_ctx == COMPLETITION_CTX_UPGRADEABLE) {
        char pwd[256];

        upgradeable_mode = 1;
        poclidek_pwd(sh_ctx.cctx, pwd, sizeof(pwd));
#if 0   /* for "installed> upgrade foo-X with foo-X"; disabled - NFY */
        if (n_str_eq(pwd, POCLIDEK_INSTALLEDDIR))
            uprev = 1;
#endif        
    }
    
    if (genpackages) {
        if (sh_ctx.completion_ctx == COMPLETITION_CTX_INSTALLED)
            ents = poclidek_get_dent_ents(sh_ctx.cctx, POCLIDEK_INSTALLEDDIR);
        else
            ents = sh_ctx.cctx->currdir->pkg_dent_ents;
        
    } else {
        ents = sh_ctx.cctx->rootdir->pkg_dent_ents;
        // completion through directory tree, NFY
        //const char *path = text ? n_dirname(n_strdup(text)) : n_strdup(".");
        //ents = poclidek_get_dents(sh_ctx.cctx, path);
        //ents = sh_ctx.cctx->rootdir->pkg_dent_ents;
        //printf("path %s, ents = %d, %s\n", path, ents ? n_array_size(ents) : 0, text);
        //free(path);
    }
    
    if (ents == NULL)
        return NULL;
    
    //printf("text %s\n", text); 
    if (state == 0) {
        len = strlen(text);
        if (len == 0)
            i = 0;
        else 
            i = n_array_bsearch_idx_ex(ents, n_basenam(text),
                                       (tn_fn_cmp)pkg_dent_strncmp);
    }
    
    while (i > -1 && i < n_array_size(ents)) {
        struct pkg_dent *ent = n_array_nth(ents, i++);
        char ent_path[PATH_MAX];
        const char *path;
        
        if (genpackages) {
            struct pkg *pkg = ent->pkg_dent_pkg;
            if (pkg_dent_isdir(ent))
                continue;
            
            if (upgradeable_mode && !is_upgradeable(sh_ctx.cctx, pkg, uprev))
                continue;

            path = ent->name;
            
        } else {
            if (!pkg_dent_isdir(ent))
                continue;
            path = ent->name;
            //path = poclidek_dent_dirpath(ent_path, sizeof(ent_path), ent);
        }
        //printf("path %s, (%s)\n", path, text); 
        if (len == 0 || strncmp(text, path, len) == 0) {
            name = path;
            break;

        } else if (len > 1 && *text == '/' &&
                   strncmp((text + 1), path, len - 1) == 0) {
            name = poclidek_dent_dirpath(ent_path, sizeof(ent_path), ent);
            break;
            
        } else if (len == 1 && *text == '/') {
            name = poclidek_dent_dirpath(ent_path, sizeof(ent_path), ent);
            break;
        }
    }

    if (name)
        return n_strdup(name);
    return NULL;
}

static char *deps_generator(const char *text, int state)
{
    static tn_array *deps_table = NULL; /* XXX static variable */
    
    if (state == 0) {
        tn_array *ents;
        int i, j, len;

        ents = sh_ctx.cctx->currdir->pkg_dent_ents;

        if (ents == NULL)
            return NULL;

        len = strlen(text);

        n_assert(deps_table == NULL);
        /* create deps_table */
        deps_table = n_array_new(n_array_size(ents) * 4, NULL, (tn_fn_cmp)strcmp);

        /* fill deps_table with data */
        for (i = 0; i < n_array_size(ents); i++) {
            struct pkg_dent *ent = n_array_nth(ents, i);
            struct pkg *pkg = ent->pkg_dent_pkg;
            tn_array *caps = NULL;

            if (pkg_dent_isdir(ent))
                continue;

            switch (sh_ctx.completion_ctx) {
                case COMPLETITION_CTX_WHAT_PROVIDES:
                    caps = pkg->caps;
                    break;

                case COMPLETITION_CTX_WHAT_REQUIRES:
                    caps = pkg->reqs;
                    break;

		case COMPLETITION_CTX_WHAT_SUGGESTS:
		    caps = pkg->sugs;
		    break;
            }

            if (caps) {
                for (j = 0; j < n_array_size(caps); j++) {
                    struct capreq *cr = n_array_nth(caps, j);
                    const char *name = capreq_name(cr);

                    if (len == 0 || strncmp(name, text, len) == 0)
                        n_array_push(deps_table, (void*)name);
                }
            }
        }
    }

    if (state >= n_array_size(deps_table)) {
        n_array_cfree(&deps_table);
        return NULL;
    }

    return n_strdup(n_array_nth(deps_table, state));
}

static char *pkgname_generator(const char *text, int state)
{
    return arg_generator(text, state, 1);
}

static char *dirname_generator(const char *text, int state)
{
    return arg_generator(text, state, 0);
}

#ifndef HAVE_READLINE_4_2
# define rl_completion_matches(a, b) completion_matches(a, b)
#endif

static char **poldek_completion(const char *text, int start, int end)
{
    char **matches = NULL;
    char *p;
    
    start = start;
    end = end;
    matches = NULL;

    p = rl_line_buffer;

    while (isspace(*p))
        p++;
    
    if (*p) {  /* XXX: alias context should be configurable, TODO */
        if (strncmp(p, "un", 2) == 0) /* uninstall cmd */
            sh_ctx.completion_ctx = COMPLETITION_CTX_INSTALLED;
        
        else if (strncmp(p, "upgr", 4) == 0) /* upgrade cmd */
            sh_ctx.completion_ctx = COMPLETITION_CTX_UPGRADEABLE;
        
        else if (strncmp(p, "gree", 4) == 0) /* greedy-upgrade cmd */
            sh_ctx.completion_ctx = COMPLETITION_CTX_UPGRADEABLE;
        
        else if (strncmp(p, "what-prov", 9) == 0) /* what-provides cmd */
            sh_ctx.completion_ctx = COMPLETITION_CTX_WHAT_PROVIDES;

        else if (strncmp(p, "what-req", 8) == 0) /* what-requires cmd */
            sh_ctx.completion_ctx = COMPLETITION_CTX_WHAT_REQUIRES;

	else if (strncmp(p, "what-sug", 8) == 0) /* what-suggests cmd */
	    sh_ctx.completion_ctx = COMPLETITION_CTX_WHAT_SUGGESTS;

        else if (strncmp(p, "cd ", 3) == 0)
            sh_ctx.completion_ctx = COMPLETITION_CTX_DIRNAME;

        else 
            sh_ctx.completion_ctx = COMPLETITION_CTX_NONE;
    }
    
    if (start == 0 || strchr(p, ' ') == NULL) {
        matches = rl_completion_matches(text, command_generator);

    } else if (strncmp(text, "--", 2) == 0) {
        matches = rl_completion_matches(text, command_options_generator);

    } else {
        rl_completer_word_break_characters = " \t\n\"\\'`$><=;|&{(";
        
        switch (sh_ctx.completion_ctx) {
            case COMPLETITION_CTX_DIRNAME:
                matches = rl_completion_matches(text, dirname_generator);
                break;
                
            case COMPLETITION_CTX_WHAT_PROVIDES:
            case COMPLETITION_CTX_WHAT_REQUIRES:
            case COMPLETITION_CTX_WHAT_SUGGESTS:
                rl_completer_word_break_characters = " \t\n\"\\'`$><=;|&{";
                matches = rl_completion_matches(text, deps_generator);
                break;
                
            default:
                matches = rl_completion_matches(text, pkgname_generator);
        }
    }
    
    return matches;
}


static void initialize_readline(void)
{
    rl_readline_name = "poldek";
    rl_attempted_completion_function = poldek_completion;
    rl_completion_entry_function = command_generator;
}

static int cmd_quit(struct cmdctx *cmdctx)
{
    cmdctx = cmdctx;
    shQuit = 1;
    return 1;
}

static void sigint_cb(void)
{
    if (!shInCmd) {
        shDone = 1;
    }
}

static void shell_end(int sig) 
{
    if (sig > 0) {
        signal(sig, SIG_DFL);
        shDone = 1;
    }
}

static int init_shell(struct poclidek_ctx *cctx) 
{
    poldek_term_init();
    sh_ctx.completion_ctx = COMPLETITION_CTX_NONE;
    sh_ctx.cctx = cctx;
    cctx->_flags |= POLDEKCLI_UNDERIMODE;
    return poclidek_add_command(cctx, &command_quit);
}


int poclidek_shell(struct poclidek_ctx *cctx)
{
    const char *prompt_prefix = "poldek";
    char *line, *s, *home;

    if (cctx->htcnf) {
        tn_hash *global = poldek_conf_get_section(cctx->htcnf, "global");
        const char *s = global ? poldek_conf_get(global, "prompt", NULL) : NULL;
        if (s) {
            prompt_prefix = s;
            DBGF("prompt_prefix %s\n", s);
        }
        
    }
    
    if (!isatty(fileno(stdout))) {
        logn(LOGERR, _("not a tty"));
        return 0;
    }

    if (!init_shell(cctx))
        exit(EXIT_FAILURE);
    
    initialize_readline();
    histfile = NULL;

    if ((home = getenv("HOME"))) {
        int len = strlen(home) + strlen("/.poldek_history") + 2;
        histfile = alloca(len);
        snprintf(histfile, len, "%s/.poldek_history", home);
        read_history(histfile);
    }

    sigint_init();
    sigint_push(sigint_cb);
    signal(SIGTERM, shell_end);
    signal(SIGQUIT, shell_end);
    
    printf(_("\nWelcome to the poldek shell mode. "
             "Type \"help\" for help with commands.\n\n"));

    shDone = 0;
    while (!shDone) {
        struct pkg_dent *currdir = sh_ctx.cctx->currdir;
        char prompt[255];
        
        sigint_reset();
        n_snprintf(prompt, sizeof(prompt), "%s:%s%s> ", prompt_prefix,
                   currdir == NULL ? "/" : *currdir->name == '/' ? "" : "/",
                   currdir == NULL ? "" : currdir->name);

        if ((line = readline(prompt)) == NULL)
            break;

        /* add to history? */
        s = line;
        while (isspace(*s))
            s++;
        
        if (*s)
            add_history(line);
                
        s = n_str_strip_ws(line);
        if (*s) {
            shInCmd = 1;
            DBGF("(%s)\n", s);

            MEMINF("BEFORE %s\n", s);
            poclidek_execline(cctx, NULL, s);
            MEMINF("AFTER  %s\n", s);
            
            sigint_reset();
            shDone = 0;
            shInCmd = 0;
        }
        free(line);
        
        signal(SIGTERM, shell_end);
        signal(SIGQUIT, shell_end);

        if (shQuit)
            shDone = 1;
    }
    
    if (histfile) 
        write_history(histfile);
    
    sigint_pop();
    msg(0, "\n");
    return 1;
}
