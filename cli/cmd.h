/* $Id$ */
#ifndef  POCLIDEK_CMD_H
#define  POCLIDEK_CMD_H

#include <argp.h>
#include <trurl/narray.h>
#include <trurl/nbuf.h>

struct poldek_ctx;
struct poldek_ts;
struct pkg;

struct poclidek_cmd;
struct cmd_pipe;

#define CMDCTX_ISHELP        (1 << 0)
#define CMDCTX_NOCTRLMSGS    (1 << 1)
#define CMDCTX_ERR           (1 << 2)
    
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

#define COMMAND__MALLOCED      (1 << 17) /* internal */

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
    void                 (*_free)(struct poclidek_cmd *);
};

int poclidek_add_command(struct poclidek_ctx *cctx, struct poclidek_cmd *cmd);
int poclidek_cmd_ncmp(struct poclidek_cmd *c1, struct poclidek_cmd *c2);


#define POCLIDEK_INSTALLEDDIR     "/installed"
#define POCLIDEK_AVAILDIR         "/all-avail"
#define POCLIDEK_HOMEDIR          POCLIDEK_AVAILDIR  

/* _flags, internal do not touch*/
#define POLDEKCLI_UNDERIMODE       (1 << 4)
#define POLDEKCLI_CONFIG_LOADED    (1 << 5)
#define POLDEKCLI_LOADED_AVAILABLE (1 << 6)
#define POLDEKCLI_LOADED_INSTALLED (1 << 7)

/* flags */
#define POCLIDEK_SKIP_INSTALLED    (1 << 0) /* if set, POCLIDEK_LOAD_RELOAD must
                                               be passed to poclidek_load_packages()
                                               to load installed packages
                                            */

struct pkg_dent;                /* package dirent struct */
struct poclidek_ctx {
    unsigned            flags;
    struct poldek_ctx   *ctx;
    tn_array            *commands;
    tn_array            *pkgs_available;   /* array of available pkgs  */
    tn_array            *pkgs_installed;   /* array of installed pkgs  */

    
    struct pkgdir       *dbpkgdir;   /* db packages */
    tn_array            *dbpkgdir_added; /* packages which was installed */
    time_t              ts_dbpkgdir; /* timestamp */

    unsigned            _flags;

    tn_alloc            *_dent_na;
    void                *(*_dent_alloc)(struct poclidek_ctx *, size_t);
    struct pkg_dent     *rootdir;
    struct pkg_dent     *homedir;
    struct pkg_dent     *currdir;
    
};

#endif
