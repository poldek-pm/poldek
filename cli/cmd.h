#ifndef POCLIDEK_CMD_H
#define POCLIDEK_CMD_H

#include <trurl/narray.h>
#define CMD_CHAIN_ENT_CMD        (1 << 0)
#define CMD_CHAIN_ENT_PIPE       (1 << 1)
#define CMD_CHAIN_ENT_SEMICOLON  (1 << 2)

struct poclidek_cmd;


#define CMD_PIPE_PIPE   1
#define CMD_PIPE_INTERN 2

struct cmd_pipe {
    int  type;
    FILE *stream_in;
    FILE *stream_out;
    tn_array *pkgs;
    int  _lineno;
};

char *cmd_pipe_getline(struct cmd_pipe *p, char *line, int size);


struct cmd_chain_ent {
    unsigned             flags;
    struct poclidek_cmd  *cmd;
    tn_array             *a_argv;
    struct cmd_chain_ent *next_piped;
    struct cmd_pipe      *left_pipe;
    struct cmd_pipe      *rigth_pipe;
};

#endif
