#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <time.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <trurl/trurl.h>

#include "pkg.h"
#include "pkgset-def.h"
#include "log.h"

static int cmd_ls();
static int cmd_stat();
static int cmd_help();
static int cmd_quit();
static int cmd_info();
static int cmd_fetch();
static int cmd_install();

struct command {
    char *name;
    char *arg;
    int (*fn)(const char **argv);
    char *doc;
};

struct command commands_tab[] = {
{"help", NULL, cmd_help, "display this help"},
{"ls", "[FILE...]",  cmd_ls, "list directory contents"},
{"quit", NULL, cmd_quit, "quit poldek"},
{"stat", "[FILE...]", cmd_stat, "display package(s) status"},
{"info", "[FILE...]", cmd_ls, "display package(s) info"},
{"fetch", "[FILE...]", cmd_fetch, "fetch package(s)"},
{"install", "[FILE...]", cmd_install, "install package(s)"},
{NULL, NULL, NULL, NULL}
};

static struct pkgset *pkgset = NULL;
static tn_array *Packages = NULL;
static tn_array *commands = NULL;
static int done = 0;

static
int command_cmp(struct command *c1, struct command *c2) 
{
    return strcmp(c1->name, c2->name);
}

static
int pkg_rl_cmp(struct pkg *pkg, const char *name) 
{
    return strncmp(pkg->name, name, strlen(name));
}


static
int execute_line(char *line)
{
    struct command *command, tmpcmd;
    char *p;
    const char **args;
    int rc;
    
    
    p = line;
    while (*p && !isspace(*p))
	p++;
    
    if (*p)
        *p++ = '\0';

    tmpcmd.name = line;
    if ((command = n_array_bsearch(commands, &tmpcmd)) == NULL) {
	fprintf(stderr, "%s: no such command.\n", line);
	return 0;
    }
    
    while (isspace(*p))
	p++;
    
    args = n_str_tokl(p, " \t");
    rc = command->fn(args);
    n_str_tokl_free(args);
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

/* **************************************************************** */
/*                                                                  */
/*                  Interface to Readline Completion                */
/*                                                                  */
/* **************************************************************** */

/* Generator function for command completion.  STATE lets us know whether
   to start from scratch; without any state (i.e. STATE == 0), then we
   start at the top of the list. */
static
char *command_generator(char *text, int state)
{
    static int list_index, len;
    char *name;

    /* If this is a new word to complete, initialize now.  This includes
       saving the length of TEXT for efficiency, and initializing the index
       variable to 0. */
    if (!state) {
	list_index = 0;
	len = strlen(text);
    }
    /* Return the next name which partially matches from the command list. */
    while ((name = commands_tab[list_index].name)) {
	list_index++;

	if (strncmp(name, text, len) == 0)
	    return strdup(name);
    }

    /* If no names matched, then return NULL. */
    return NULL;
}

static
char *pkg_name_generator(char *text, int state)
{
    static int i, len;
    char *name = NULL;
    
    if (state == 0) {
        len = strlen(text);
        if (len == 0)
            i = 0;
        else 
            i = n_array_bsearch_idx_ex(pkgset->pkgs, text,
                                       (tn_fn_cmp)pkg_rl_cmp);
    }

    if (i > -1 && i < n_array_size(pkgset->pkgs)) {
        struct pkg *pkg = n_array_nth(pkgset->pkgs, i++);
        if (len == 0 || strncmp(pkg->name, text, len) == 0) 
            name = strdup(pkg->name);
    }
    
    return name;
}

/* Attempt to complete on the contents of TEXT.  START and END bound the
   region of rl_line_buffer that contains the word to complete.  TEXT is
   the word to complete.  We can use the entire contents of rl_line_buffer
   in case we want to do some simple parsing.  Return the array of matches,
        or NULL if there aren't any. */
static
char **poldek_completion(char *text, int start, int end)
{
    char **matches;

    
    end = end;
    matches = (char **) NULL;
    
    /* If this word is at the start of the line, then it is a command
       to complete.  Otherwise it is the name of a file in the current
       directory. */
//    fprintf(stderr, "text = %s, start %d, end %d\n");
    if (start == 0)
	matches = completion_matches(text, command_generator);
    else
        matches = completion_matches(text, pkg_name_generator);
    return (matches);
}


/* Tell the GNU Readline library how to complete.  We want to try to complete
   on command names if this is the first word in the line, or on filenames
   if not.
*/
static
void initialize_readline(void)
{
    rl_readline_name = "poldek";
    rl_attempted_completion_function = (CPPFunction *) poldek_completion;
    rl_completion_entry_function = (Function *) pkg_name_generator;
}

static 
void prepare_argv(char **argv, tn_array **optsp, tn_array **pkgsp)
{
    tn_array *pkgs = NULL, *opts = NULL;
    char *arg;
    int i, n = 0;

    *optsp = NULL;
    *pkgsp = NULL;
    
    if (argv == NULL) 
        return;

    opts = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    pkgs = n_array_new(16, NULL, (tn_fn_cmp)pkg_cmp_name_evr);
    
    n = 0;
    while (argv[n]) {
        if (*argv[n] != '-')
            break;

        n_array_push(opts, argv[n++]);
    }

    *optsp = opts;
    if (argv[n] == NULL)
        return;
    
    while ((arg = argv[n++])) {
        int asterisk = 0, len;
        char *p;
        
        if ((p = strchr(arg, '*')) && *(p+1) == '\0') {
            *p = '\0';
            i = n_array_bsearch_idx_ex(pkgset->pkgs, arg,
                                       (tn_fn_cmp)pkg_rl_cmp);
            asterisk = 1;
            len = strlen(arg);
                
        } else {
            struct pkg tmpkg;
                
            tmpkg.name = arg;
            i = n_array_bsearch_idx_ex(pkgset->pkgs, &tmpkg,
                                       (tn_fn_cmp)pkg_cmp_name);
        }
            
        if (i == -1) {
            printf("%s: no such package\n", arg);
            continue;
        }
            
        n_array_push(pkgs, n_array_nth(pkgset->pkgs, i++));
        while (i < n_array_size(pkgset->pkgs)) {
            struct pkg *pkg = n_array_nth(pkgset->pkgs, i++);
                
            if (asterisk) {
                if (strncmp(pkg->name, arg, len) == 0)
                    n_array_push(pkgs, pkg);
                    
            } else {
                if (strcmp(pkg->name, arg) == 0)
                    n_array_push(pkgs, pkg);
            }
        }
    }
    

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        n_array_free(opts);
        pkgs = NULL;
    } else {
        n_array_sort(pkgs);
        n_array_uniq(pkgs);
    }

    
    *pkgsp = pkgs;
}


static
int cmd_list(char **argv)
{
    printf("ls ");
    if (argv) {
        int n = 0;
        while (argv[n]) 
            printf("%s ", argv[n++]);
    }
    printf("\n");
    
            
    return 1;
}


#define LSFMT_BRIEF 0
#define LSFMT_LONG  1

static int cmd_ls(char **argv)
{
    tn_array *pkgs = NULL, *opts = NULL;
    int lsfmt = LSFMT_BRIEF;
    int i, size, err = 0;
    
    if (argv == NULL)
        return 0;

    prepare_argv(argv, &opts, &pkgs);

    if (pkgs == NULL) 
        return 0;

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = pkgset->pkgs;
    }

    lsfmt = LSFMT_BRIEF;
    
    for (i=0; i<n_array_size(opts); i++) {
        char *opt = n_array_nth(opts, i); 
        if (strcmp(opt, "-l") == 0) {
            lsfmt = LSFMT_LONG;
            printf("%-42s%-12s%16s\n", "package", "build date", "size (kB)");

        } else {
            printf("ls: %s: unknown option\n", opt);
            err++;
        }
    }

    if (err) 
        goto l_end;
    
    size = 0;
    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        switch (lsfmt) {
            case LSFMT_BRIEF:
                printf("%s\n", pkg->name);
                break;
                
            case LSFMT_LONG: {
                char timbuf[30];
                if (pkg->btime) 
                    strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M",
                             localtime((time_t*)&pkg->btime));
                else
                    *timbuf = '\0';
                printf("%-42s%12s%8d\n", pkg_snprintf_s(pkg),
                       timbuf, pkg->size/1024);
                size += pkg->size/1024;
            }
            break;
            
            default:
                n_assert(0);
        }
    }

    if (lsfmt != LSFMT_BRIEF && n_array_size(pkgs)) {
        char *unit;
        int val;
        
        if (size > 1000) {
            unit = "MB";
            val = size/1000;
        } else {
            unit = "kB";
            val = size;
        }
        
        printf("%d package%s, %d %s\n", n_array_size(pkgs),
               n_array_size(pkgs) > 1 ? "s":"", val, unit);
    }

 l_end:
    if (opts)
        n_array_free(opts);
    
    if (pkgs != pkgset->pkgs)
        n_array_free(pkgs);
    
    return err == 0;
}


static int cmd_install(char **argv)
{
    tn_array *pkgs = NULL, *opts = NULL;
    int lsfmt = LSFMT_BRIEF;
    int i, size, err = 0;
    
    if (argv == NULL)
        return 0;

    prepare_argv(argv, &opts, &pkgs);

    if (pkgs == NULL) 
        return 0;

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = pkgset->pkgs;
    }

    lsfmt = LSFMT_BRIEF;

    printf("install ");
    for (i=0; i<n_array_size(opts); i++) {
        char *opt = n_array_nth(opts, i); 
        if (strcmp(opt, "-f") == 0) {
            printf("force ");
 
        } else {
            printf("ls: %s: unknown option\n", opt);
            err++;
        }
    }

    if (err) 
        goto l_end;
    
    size = 0;
    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        printf("%s ", pkg_snprintf_s(pkg));
    }


 l_end:
    if (opts)
        n_array_free(opts);
    
    if (pkgs != pkgset->pkgs)
        n_array_free(pkgs);
    
    return err == 0;
}



static int cmd_fetch(char **argv)
{
    printf("stat");
    return (0);
}




static
int cmd_stat(char **argv)
{
    printf("stat");
    return (0);
}

static
int cmd_help(char **argv)
{
    int i;
    
    for (i=0; i<n_array_size(commands); i++) {
        char buf[60];
        struct command *cmd = n_array_nth(commands, i);

        snprintf(buf, sizeof(buf), "%s %s", cmd->name,
                 cmd->arg ? cmd->arg : "");
        printf("%-30s %s\n", buf, cmd->doc);
    }
    return 0;
}

static
int cmd_quit(char *arg)
{
    done = 1;
    return (0);
}

#if 0
static
int valid_argument(char *caller, char *arg)
{
    if (!arg || !*arg) {
	fprintf(stderr, "%s: an argument required.\n", caller);
	return (0);
    }
    
    return (1);
}
#endif

int shell_main(struct pkgset *ps)
{
    char *line, *s;
    int n;
    
    if (pkgset != NULL) {
        log(LOGERR, "shell_main: not reentrant func\n");
        return 0;
    }
    
    initialize_readline();
    pkgset = ps;
    
    n = 0;
    commands = n_array_new(16, NULL, (tn_fn_cmp)command_cmp);
    while(commands_tab[n].name)
        n_array_push(commands, &commands_tab[n++]);
    n_array_sort(commands);
    
    
    while (done == 0) {
	if ((line = readline("poldek-> ")) == NULL)
            break;
        
	s = stripwhite(line);
	if (*s) {
	    add_history(s);
	    execute_line(s);
	}
	free(line);
    }
    pkgset = NULL;
    return 0;
}
