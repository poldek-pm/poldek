/* $Id$ */
#ifndef  POLDEK_SHELL_H
#define  POLDEK_SHELL_H

#include <argp.h>
#include <time.h>
#include <signal.h>

#include "poldek.h"
#include "pkg.h"
#include "log.h"
#include "poldek_term.h"

#define POLDEKCLI_CONFIG_LOADED   (1 << 0)
#define POLDEKCLI_PACKAGES_LOADED (1 << 1)

struct poldekcli_ctx {
    
    struct poldek_ctx   *ctx;
    tn_array            *commands;
    tn_array            *aliases;
    tn_array            *all_commands;
    tn_array            *avpkgs;     /* array of available pkgs  */
    tn_array            *avail_plist;
    tn_array            *instd_plist;
    tn_array            *instpkgs;   /* array of installed pkgs  */
    
    time_t         ts_instpkgs; /* instpkgs timestamp */
    struct pkgdir  *dbpkgdir;   /* db packages        */

    unsigned            flags;
    char                *config_path;

    tn_array  *arg_packages;     /* foo[-VERSION[-RELEASE]] || foo.rpm   */
    tn_array  *arg_package_sets; /* --pset ftp://ftp.zenek.net/PLD/tiny */

    struct usrpkgset  *ups;

};


int poldekcli_init(struct poldekcli_ctx *cctx, struct poldek_ctx *ctx,
                   int skip_installed);

int poldekcli_load_packages(struct poldekcli_ctx *cctx, int skip_installed);

void poldekcli_destroy(struct poldekcli_ctx *cctx);

int poldekcli_exec(struct poldekcli_ctx *cctx, int argc, char **argv);

void sh_resolve_packages(tn_array *pkgnames, tn_array *avpkgs,
                         tn_array **pkgsp, int strict);


/*
  search pkg in pkgs, if found compare versions and store result in cmprc
  and if evr isn't NULL store EVR in it.  
 */
int pkg_cmp_lookup(struct pkg *pkg, tn_array *pkgs,
                     int compare_ver, int *cmprc,
                     char *evr, size_t size);

#define COMMAND_ARGS_PKGS_AV      1
#define COMMAND_ARGS_PKGS_AVUPGR  2
#define COMMAND_ARGS_PKGS_INST    3

struct cmdarg {
    struct poldekcli_ctx *cctx;     /* common shell_s struct */
    tn_array         *pkgnames; /* arguments */
    tn_array         *pkgs;     /* resolved arguments */
    
    unsigned         flags;     /* cmd private flags */
    int              is_help;   /*  */
    void             *d;        /* cmd private data */
};


#define COMMAND_NOARGS       (1 << 0) /* cmd don't accept arguments */
#define COMMAND_NOOPTS       (1 << 1) /* cmd don't accept options */
#define COMMAND_NOHELP       (1 << 2) /* cmd hasn't help */
#define COMMAND_EMPTYARGS    (1 << 3) /* cmd accepts empty arg list */
#define COMMAND_HASVERBOSE   (1 << 4) /* cmd has verbose command */

#define COMMAND_MODIFIESDB   (1 << 8)

struct command {
    unsigned            flags;
    char                *name;
    char                *arg;
    char                *doc;
    struct argp_option  *argp_opts;
    
    error_t (*parse_opt_fn)(int, char*, struct argp_state*);
    
    int (*cmd_fn)(struct cmdarg *, int, const char **, struct argp*);
    int (*do_cmd_fn)(struct cmdarg *);
    
    void* (*init_cmd_arg_d)(void);
    void  (*destroy_cmd_arg_d)(void*);
    
    char                 *extra_help;
};

struct command_alias {
    char                *name;      /* alias name    */
    char                *cmdline;   /* alias content */
    struct command      *cmd;
};


int sh_printf_c(FILE *stream, int color, const char *fmt, ...);

extern int shOnTTY;
extern volatile sig_atomic_t shSIGINT;

/* alias.c */
void poldekcli_load_aliases(struct poldekcli_ctx *cctx, const char *path);

#endif 
