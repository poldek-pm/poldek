/* $Id$ */
#ifndef  POCLIDEK_CLI_H
#define  POCLIDEK_CLI_H

#include <argp.h>
#include <trurl/narray.h>
#include "poldek.h"
#include "pkg.h"
#include "log.h"
#include "poldek_term.h"
#include "dent.h"

#define POCLIDEK_INSTALLEDDIR     "/installed"
#define POCLIDEK_AVAILDIR         "/all-avail"

#define POLDEKCLI_SKIPINSTALLED   (1 << 0)
#define POLDEKCLI_CONFIG_LOADED   (1 << 5)
#define POLDEKCLI_PACKAGES_LOADED (1 << 6)
#define POLDEKCLI_CMDL            (1 << 7)

struct poclidek_ctx;

struct poclidek_ctx {
    struct poldek_ctx   *ctx;
    tn_array            *commands;
    tn_array            *pkgs_available;   /* array of available pkgs  */
    tn_array            *pkgs_installed;   /* array of installed pkgs  */
    
    struct pkgdir       *dbpkgdir;   /* db packages */
    time_t              ts_dbpkgdir; /* timestamp */

    unsigned            flags;

    tn_alloc            *_dent_na;
    void                *(*dent_alloc)(struct poclidek_ctx *, size_t);
    struct pkg_dent     *rootdir;
    struct pkg_dent     *homedir;
    struct pkg_dent     *currdir;
};


int poclidek_init(struct poclidek_ctx *cctx, struct poldek_ctx *ctx);
void poclidek_destroy(struct poclidek_ctx *cctx);
int poclidek_load_packages(struct poclidek_ctx *cctx);



/* cmdctx */
struct cmd_pipe;

#define CMDCTX_ISHELP        (1 << 0)
#define CMDCTX_NOCTRLMSGS    (1 << 1)
    
struct cmdctx {
    int                  rtflags;   /* CMDCTX_* */
    struct poclidek_ctx  *cctx;     /* common shell_s struct */
    struct poldek_ts     *ts;
    struct poclidek_cmd  *cmd;
    
    unsigned             _flags;     /* cmd private flags */
    void                 *_data;     /* cmd private data */
    struct cmd_pipe      *pipe_left;
    struct cmd_pipe      *pipe_right;
};
int cmdctx_addtoresult(struct cmdctx *cmdctx, struct pkg *pkg);
int cmdctx_printf(struct cmdctx *cmdctx, const char *fmt, ...);
int cmdctx_printf_c(struct cmdctx *cmdctx, int color, const char *fmt, ...);


/* poclidek_cmd */
#define COMMAND_NOARGS       (1 << 0) /* cmd don't accept arguments */
#define COMMAND_NOOPTS       (1 << 1) /* cmd don't accept options */
#define COMMAND_NOHELP       (1 << 2) /* cmd hasn't help */
#define COMMAND_EMPTYARGS    (1 << 3) /* cmd accepts empty arg list */
#define COMMAND_HASVERBOSE   (1 << 4) /* cmd has verbose command */
#define COMMAND_HIDDEN       (1 << 5) /* help doesn't displays it */


#define COMMAND_MODIFIESDB   (1 << 8)
#define COMMAND_IS_ALIAS     (1 << 9)

#define COMMAND_PIPEABLE_LEFT  (1 << 13)
#define COMMAND_PIPEABLE_RIGTH (1 << 14)
#define COMMAND_PIPEABLE COMMAND_PIPEABLE_LEFT | COMMAND_PIPEABLE_RIGTH/* both */

#define COMMAND_PIPE_XARGS     (1 << 15) /* cmd treats pipe content as arguments */
#define COMMAND_PIPE_PACKAGES  (1 << 16) /* cmd treats pipe content as packages */

#define COMMAND_PIPE_DEFAULTS  COMMAND_PIPEABLE | COMMAND_PIPE_XARGS | \
                               COMMAND_PIPE_PACKAGES

struct poclidek_cmd {
    unsigned            flags;
    char                *name;
    char                *arg;
    char                *doc;
    struct argp_option  *argp_opts;
     
    error_t (*parse_opt_fn)(int, char*, struct argp_state*);
    
    int (*cmd_fn)(struct cmdctx *, int, const char **, struct argp*);
    int (*do_cmd_fn)(struct cmdctx *);
    
    void* (*init_cmd_arg_d)(void);
    void  (*destroy_cmd_arg_d)(void*);
    
    char                 *extra_help;

    char                 *cmdline;   /* alias content */
    int                  _seqno;
};

int poclidek_add_command(struct poclidek_ctx *cctx, struct poclidek_cmd *cmd);
int poclidek_cmd_ncmp(struct poclidek_cmd *c1, struct poclidek_cmd *c2);

void poclidek_load_aliases(struct poclidek_ctx *cctx, const char *path);

struct poldek_iinf;
void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct poldek_iinf *iinf);


int poclidek_save_installedcache(struct poclidek_ctx *cctx,
                                 struct pkgdir *pkgdir);
int poclidek_load_installed(struct poclidek_ctx *cctx, int reload);

#endif 
