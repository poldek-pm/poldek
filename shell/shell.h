/* $Id$ */
#ifndef  POLDEK_SHELL_H
#define  POLDEK_SHELL_H

#define SHPKG_INSTALL   (1 << 0)
#define SHPKG_UNINSTALL (1 << 1)

#include <argp.h>

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
int shpkg_ncmp_str(struct shpkg *pkg, const char *name);


struct shell_s {
    struct pkgset  *pkgset;
    struct inst_s  *inst;
    unsigned       inst_flags_orig;
    tn_array       *avpkgs;     /* array of shpkgs  */
    tn_array       *instpkgs;   /* array of shpkgs  */
};

int get_term_width(void);
void sh_resolve_packages(tn_array *pkgnames, tn_array *avshpkgs,
                         tn_array **pkgsp, int strict);



//#define CMD_ARG_NOPKGS   (1 << 0) /* command doesn't needs packages   */
//#define CMD_ARG_ALLPKGS  (1 << 1) /* command can operate on whole set */

struct cmdarg {
    tn_array         *pkgnames; /* arguments */
    tn_array         *shpkgs;   /* resolved arguments */
    struct shell_s   *sh_s;     /* common shell_s struct */
    unsigned         flags;     /* cmd private flags */
    int              is_help;
    void             *d;        /* cmd private data */
};

struct command_alias {
    char                *name;
    char                *arg;
    char                *doc;
};



#define COMMAND_NOARGS       (1 << 0) /* cmd don't accept arguments */
#define COMMAND_NOOPTS       (1 << 1) /* cmd don't accept options */
#define COMMAND_NOHELP       (1 << 2) /* cmd hasn't help */
#define COMMAND_EMPTYARGS    (1 << 3) /* cmd accepts empty arg list */
#define COMMAND_HASVERBOSE   (1 << 4) /* cmd has verbose command */

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
};

#endif 
