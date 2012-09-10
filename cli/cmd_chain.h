/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POCLIDEK_CMDCHAIN_H
#define POCLIDEK_CMDCHAIN_H

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

tn_array *poclidek_prepare_cmdline(struct poclidek_ctx *cctx, const char *line);

#endif
