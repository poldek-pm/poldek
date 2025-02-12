/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).
*/

#ifndef POLDEK_BOOLDEP_H
#define POLDEK_BOOLDEP_H

#include <stdint.h>
#include <trurl/narray.h>

struct booldep;
struct booldep *booldep_parse(const char *expr);
void booldep_free(struct booldep *dep);

struct capreq;
struct booldep_eval_ctx {
    void *ctx;
    int (*req_cost)(const struct capreq *req, tn_array **providers, void *ctx);
};

tn_array *booldep_eval(struct booldep *dep, const struct booldep_eval_ctx *ctx);


#ifdef POLDEK_BOOLDEP_INTERNAL
#define OP_AND      (1 << 0)
#define OP_OR       (1 << 1)
#define OP_IF       (1 << 2)
#define OP_UNLESS   (1 << 3)
#define OP_ELSE     (1 << 4)
#define OP_WITH     (1 << 5)
#define OP_WITHOUT  (1 << 6)

#define NARY_MAX 3

struct node {
    uint8_t        type;
    uint8_t        ary;
    struct capreq  *cap;               /* identifier */
    struct node    *args[NARY_MAX];    /* op args */
    char           name[0];
};

struct booldep {
    struct node *node;
    char expr[0];
};
#endif

#endif
