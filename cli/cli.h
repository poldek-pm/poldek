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

#define POLDEKCLI_PKGCTX_AVAIL    1
#define POLDEKCLI_PKGCTX_INSTD    2

extern int shOnTTY;

struct poclidek_ctx {
    int                 pkg_ctx;
    struct poldek_ctx   *ctx;
    tn_array            *commands;
    tn_array            *avpkgs;     /* array of available pkgs  */
    tn_array            *instpkgs;   /* array of installed pkgs  */
    
    time_t         ts_instpkgs; /* instpkgs timestamp */
    struct pkgdir  *dbpkgdir;   /* db packages        */

    unsigned            flags;
};

#define poclidek_set_pkgctx(cctx, pkgctx) (cctx->pkg_ctx = pkgctx)
tn_array *poclidek_get_current_pkgs(struct poclidek_ctx *cctx);

int poclidek_init(struct poclidek_ctx *cctx, struct poldek_ctx *ctx,
                   int skip_installed);

int poclidek_load_packages(struct poclidek_ctx *cctx, int skip_installed);

void poclidek_destroy(struct poclidek_ctx *cctx);

int poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts, 
                   int argc, const char **argv);

int poclidek_exec_line(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                       const char *cmdline);

tn_array *poclidek_resolve_packages(struct poclidek_ctx *cctx,
                                     struct poldek_ts *ts,
                                     int exact);

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
    struct poclidek_ctx *cctx;     /* common shell_s struct */
    struct poldek_ts     *ts;
    
    unsigned            flags;     /* cmd private flags */
    int                 is_help;   /*  */
    void                *d;        /* cmd private data */
};


#define COMMAND_NOARGS       (1 << 0) /* cmd don't accept arguments */
#define COMMAND_NOOPTS       (1 << 1) /* cmd don't accept options */
#define COMMAND_NOHELP       (1 << 2) /* cmd hasn't help */
#define COMMAND_EMPTYARGS    (1 << 3) /* cmd accepts empty arg list */
#define COMMAND_HASVERBOSE   (1 << 4) /* cmd has verbose command */

#define COMMAND_MODIFIESDB   (1 << 8)
#define COMMAND_IS_ALIAS     (1 << 9)
#define COMMAND_IS_BREAK     (1 << 10)
#define COMMAND_IS_PIPE      (1 << 11)

struct poclidek_cmd {
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

    char                 *cmdline;   /* alias content */
};


struct poclidek_cmd_result {
    tn_array    *pkgs;
    tn_array    *lines;
    void  (*display)(void*);
};


int poclidek_cmd_ncmp(struct poclidek_cmd *c1, struct poclidek_cmd *c2);

int sh_printf_c(FILE *stream, int color, const char *fmt, ...);

void poclidek_load_aliases(struct poclidek_ctx *cctx, const char *path);
struct install_info;
void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct install_info *iinf);
#endif 
