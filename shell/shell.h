/* $Id$ */
#ifndef  POLDEK_SHELL_H
#define  POLDEK_SHELL_H

#define SHPKG_INSTALL   (1 << 0)
#define SHPKG_UNINSTALL (1 << 1)


#include <argp.h>
#include <time.h>

#include "pkg.h"
#include "log.h"

#include "term.h"


#define SHPKG_INSTALL   (1 << 0)
#define SHPKG_UNINSTALL (1 << 1)

struct shpkg {
    struct pkg  *pkg;
    unsigned    flags;
    int16_t     _ucnt;
    char        nevr[0];
};

struct shpkg *shpkg_link(struct shpkg *shpkg);
void shpkg_free(struct shpkg *shpkg);

int shpkg_cmp(struct shpkg *p1, struct shpkg *p2);
int shpkg_cmp_rev(struct shpkg *p1, struct shpkg *p2);
int shpkg_ncmp_str(struct shpkg *pkg, const char *name);
int shpkg_cmp_btime(struct shpkg *p1, struct shpkg *p2);
int shpkg_cmp_btime_rev(struct shpkg *p1, struct shpkg *p2);
int shpkg_cmp_bday(struct shpkg *p1, struct shpkg *p2);
int shpkg_cmp_bday_rev(struct shpkg *p1, struct shpkg *p2);

struct shell_s {
    struct pkgset  *pkgset;
    struct inst_s  *inst;
    unsigned       inst_flags_orig;
    tn_array       *avpkgs;     /* array of available shpkgs  */
    tn_array       *instpkgs;   /* array of installed shpkgs  */
    time_t         ts_instpkgs; /* instpkgs timestamp */
    struct pkgdir  *dbpkgdir;   /* db packages        */
};


void sh_resolve_packages(tn_array *pkgnames, tn_array *avshpkgs,
                         tn_array **pkgsp, int strict);




struct cmdarg {
    tn_array         *pkgnames; /* arguments */
    tn_array         *shpkgs;   /* resolved arguments */
    struct shell_s   *sh_s;     /* common shell_s struct */
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

struct command_alias {
    char                *name;      /* alias name    */
    char                *cmdline;   /* alias content */
    struct command      *cmd;
};

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
    
    struct command_alias *aliases;
    char                 *extra_help;
};


int sh_printf_c(FILE *stream, int color, const char *fmt, ...);

extern int shOnTTY;

#endif 
