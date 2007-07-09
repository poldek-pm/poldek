/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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

#include <sigint/sigint.h>
#include "i18n.h"
#include "log.h"
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

struct sh_ctx {
    int completion_ctx;
    struct poclidek_ctx  *cctx;
};

static struct sh_ctx sh_ctx = { COMPLETITION_CTX_NONE, NULL };

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
            //DBGF_F("%s %s %d (%d)\n", pkg_id(pkg), pkg_id(ipkg), cmprc, reverse);
            
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


static
char *arg_generator(const char *text, int state, int genpackages)
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

static
char *pkgname_generator(const char *text, int state)
{
    return arg_generator(text, state, 1);
}

static
char *dirname_generator(const char *text, int state)
{
    return arg_generator(text, state, 0);
}


#ifndef HAVE_READLINE_4_2
# define rl_completion_matches(a, b) completion_matches(a, b)
#endif

static
char **poldek_completion(const char *text, int start, int end)
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
        
        else 
            sh_ctx.completion_ctx = COMPLETITION_CTX_NONE;
    }
    
    if (start == 0 || strchr(p, ' ') == NULL) {
        matches = rl_completion_matches(text, command_generator);
        
    } else {
        if (strncmp(p, "cd ", 3) == 0)
            matches = rl_completion_matches(text, dirname_generator);
        else 
            matches = rl_completion_matches(text, pkgname_generator);
    }
    
    return matches;
}


static
void initialize_readline(void)
{
    rl_readline_name = "poldek";
    rl_attempted_completion_function = poldek_completion;
    rl_completion_entry_function = command_generator;
}

static
int cmd_quit(struct cmdctx *cmdctx)
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
    char *line, *s, *home;
    
    
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
        snprintf(prompt, sizeof(prompt), "poldek:%s%s> ",
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
            int _verbose = poldek_verbose();
            
            shInCmd = 1;
            DBGF("(%s)\n", s);

            MEMINF("BEFORE %s\n", s);
            poclidek_execline(cctx, NULL, s);
            MEMINF("AFTER  %s\n", s);
            
            sigint_reset();
            shDone = 0;
            shInCmd = 0;

            poldek_set_verbose(_verbose);
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
