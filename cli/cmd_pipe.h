#ifndef POCLIDEK_CMD_PIPE_H
#define POCLIDEK_CMD_PIPE_H

#include <trurl/narray.h>

struct cmd_pipe {
    int       _refcnt;
    tn_array *pkgs;
    int       nread_pkgs;
    
    tn_buf   *nbuf;
    tn_buf_it nbuf_it;
    int       nwritten;
};


struct cmd_pipe *cmd_pipe_new(void);
void cmd_pipe_free(struct cmd_pipe *pipe);
struct cmd_pipe *cmd_pipe_link(struct cmd_pipe *p);

int cmd_pipe_writepkg(struct cmd_pipe *p, struct pkg *pkg);
struct pkg *cmd_pipe_getpkg(struct cmd_pipe *p);

int cmd_pipe_writeline(struct cmd_pipe *p, const char *line, int size);
char *cmd_pipe_getline(struct cmd_pipe *p, char *line, int size);

#define CMD_PIPE_CTX_PACKAGES 0
#define CMD_PIPE_CTX_ASCII    1

int cmd_pipe_size(struct cmd_pipe *p, int pctx);
tn_array *cmd_pipe_xargs(struct cmd_pipe *p, int pctx);

int cmd_pipe_writeout_fd(struct cmd_pipe *p, int fd);

int cmd_pipe_printf(struct cmd_pipe *p, const char *fmt, ...);
int cmd_pipe_vprintf(struct cmd_pipe *p, const char *fmt, va_list args);

#endif
