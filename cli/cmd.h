#ifndef POCLIDEK_CMD_H
#define POCLIDEK_CMD_H

#include <stdint.h>
#include <trurl/narray.h>
#define CMD_CHAIN_ENT_CMD        (1 << 0)
#define CMD_CHAIN_ENT_PIPE       (1 << 1)
#define CMD_CHAIN_ENT_SEMICOLON  (1 << 2)
#define CMD_CHAIN_ENT_AND        (1 << 3)

struct cmd_pipe;
struct cmd_chain_ent {
    unsigned             flags;
    struct poclidek_cmd  *cmd;
    tn_array             *a_argv;
    struct cmd_chain_ent *next_piped;
    struct cmd_chain_ent *prev_piped;
    struct cmd_pipe      *pipe_right;
};

#endif
