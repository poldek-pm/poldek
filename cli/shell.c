/* 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
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

#include <readline/readline.h>
#include <readline/history.h>
#include <trurl/trurl.h>

#include <sigint/sigint.h>
#include "i18n.h"
#include "cli.h"

//int shOnTTY = 0;

static volatile sig_atomic_t shDone   = 0;
static volatile sig_atomic_t shInCmd  = 0;

static unsigned argp_parse_flags = ARGP_NO_EXIT;

static int argv_is_help(int argc, const char **argv);

static int cmd_quit(struct cmdarg *cmdarg);
struct poclidek_cmd command_quit = {
    COMMAND_NOARGS | COMMAND_NOHELP | COMMAND_NOOPTS,
    "quit", NULL, N_("Quit poldek"), NULL, NULL, NULL, cmd_quit,
    NULL, NULL, NULL, NULL
};

static int cmd_help(struct cmdarg *cmdarg);
struct poclidek_cmd command_help = {
    COMMAND_NOARGS | COMMAND_NOHELP | COMMAND_NOOPTS,
    "help", NULL, N_("Display this help"), NULL, NULL, NULL, cmd_help,
    NULL, NULL, NULL, NULL
};
#if 0
static 
int cmd_reload(struct cmdarg *cmdarg,
               int argc, const char **argv, struct argp *argp);

struct poclidek_cmd command_reload = {
    COMMAND_NOARGS | COMMAND_NOOPTS, "reload", NULL,
    N_("Reload installed packages"),
    NULL, NULL, cmd_reload, NULL, NULL, NULL, NULL, NULL
};
#endif

static char *histfile;


#define CMPLT_CTX_AVPKGS       0
#define CMPLT_CTX_AVPKGS_UPGR  1
#define CMPLT_CTX_INSTPKGS     2

struct sh_ctx {
    struct poclidek_ctx  *cctx;
};

static struct sh_ctx sh_ctx = { NULL };

static
char *command_generator(const char *text, int state)
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
    static int           i, len;
    char                 *name = NULL;
    tn_array             *ents;

    
    ents = sh_ctx.cctx->currdir->pkg_dent_ents;
    if (!genpackages) {
        ents = sh_ctx.cctx->rootdir->pkg_dent_ents;
        // completion through directory tree, DUPA
        //const char *path = text ? n_dirname(n_strdup(text)) : n_strdup(".");
        //ents = poclidek_get_dents(sh_ctx.cctx, path);
        //ents = sh_ctx.cctx->rootdir->pkg_dent_ents;
        //printf("path %s, ents = %d, %s\n", path, ents ? n_array_size(ents) : 0, text);
        //free(path);
    }
    
    
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
        char ent_path[PATH_MAX], *path;
        
        if (genpackages) {
            if (pkg_dent_isdir(ent))
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
        name = n_strdup(name);
    
    return name;
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
    
#if 0
    if (*p) {
        if (strncmp(p, "un", 2) == 0) /* uninstall cmd */
            switch_pkg_completion(CMPLT_CTX_INSTPKGS);
        
        else if (strncmp(p, "upgr", 4) == 0) /* upgrade cmd */
            switch_pkg_completion(CMPLT_CTX_AVPKGS_UPGR);
        
        else if (strncmp(p, "gree", 4) == 0) /* greedy-upgrade cmd */
            switch_pkg_completion(CMPLT_CTX_AVPKGS_UPGR);
        
        else 
            switch_pkg_completion(CMPLT_CTX_AVPKGS);
    }
#endif
    
    if (start == 0 || strchr(p, ' ') == NULL) 
        matches = rl_completion_matches(text, command_generator);
    else {
        if (strncmp(p, "cd ", 3) == 0) {
            matches = rl_completion_matches(text, dirname_generator);
        } else 
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
    
    printf("%s\n", poldek_BANNER);
#if 0
    while (commands_tab[i]) {
        poclidek_cmd *cmd = commands_tab[i++];
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
#endif    
    return 0;
}

static
int cmd_quit(struct cmdarg *cmdarg)
{
    cmdarg = cmdarg;
    shDone = 1;
    return 1;
}

static void sigint_cb(void)
{
    if (!shInCmd)
        shDone = 1;
}

static void shell_end(int sig) 
{
    if (sig > 0) {
        signal(sig, SIG_DFL);
        shDone = 1;
    }
}


static void sigint_reached_fn(void)
{
    logn(LOGNOTICE, "interrupt signal reached");
}

static void init_shell(struct poclidek_ctx *cctx) 
{
    sh_ctx.cctx = cctx;
}

int poclidek_shell(struct poclidek_ctx *cctx)
{
    char *line, *s, *home;
    
    
    if (!isatty(fileno(stdout))) {
        logn(LOGERR, _("not a tty"));
        return 0;
    }
    shOnTTY = 1;

    init_shell(cctx);
    term_init();
    initialize_readline();
    histfile = NULL;

    if ((home = getenv("HOME"))) {
        int len = strlen(home) + strlen("/.poldek_history") + 2;
        histfile = alloca(len);
        snprintf(histfile, len, "%s/.poldek_history", home);
        read_history(histfile);
    }

    sigint_init();
    sigint_reached_cb = sigint_reached_fn;
    sigint_push(sigint_cb);
    signal(SIGTERM, shell_end);
    signal(SIGQUIT, shell_end);
    
    printf(_("\nWelcome to the poldek shell mode. "
             "Type \"help\" for help with commands.\n\n"));

    shDone = 0;
    while (!shDone) {
        char prompt[255];
        
        sigint_reset();
        snprintf(prompt, sizeof(prompt), "poldek:%s%s> ",
                 *sh_ctx.cctx->currdir->name == '/' ? "" : "/",
                 sh_ctx.cctx->currdir->name);
        if ((line = readline(prompt)) == NULL)
            break;

        s = line;
        s = strip(line);
        if (*s) {
            add_history(s);
            //print_mem_info("BEFORE");
            poclidek_exec_line(cctx, NULL, s);
            //print_mem_info("AFTER ");
        }
        free(line);

        signal(SIGTERM, shell_end);
        signal(SIGQUIT, shell_end);
    }

    if (histfile) 
        write_history(histfile);

    sigint_pop();
    return 1;
}
