/*
  Copyright (C) 2020 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <trurl/trurl.h>
#include <trurl/nbuf.h>

#include "capreq.h"
#include "log.h"

#define POLDEK_BOOLDEP_INTERNAL
#include "booldep.h"

static struct op {
    const char *name;
    uint8_t    len;
    uint8_t    type;
    uint8_t    ary;
} operators[] = {
    { "and",     3, OP_AND, 2 },
    { "or",      2, OP_OR, 2 },
    { "if",      2, OP_IF, 3 },
    { "unless",  6, OP_UNLESS, 3 },
    { "else",    4, OP_ELSE, 0 },
    { "with",    4, OP_WITH, 2 },
    { "without", 7, OP_WITHOUT, 2 },
    { NULL, 0, 0, 0},
};

static struct node *node_new(int type, int ary,
                             const char *name, char *evr, unsigned relflags)
{
    int len = strlen(name);

    struct node *node = malloc(sizeof(*node) + len + 1);
    node->type = type;
    node->ary = ary;

    node->cap = NULL;
    if (node->type == 0) {        /* identifier */
        node->cap = capreq_new_evr(NULL, name, evr, relflags, CAPREQ_BASTARD);
    }

    n_strncpy(node->name, name, len + 1);
    memset(node->args, 0, sizeof(node->args));

    return node;
}

static void node_free(struct node *node)
{
    for (int i = 0; i < NARY_MAX; i++) {
        struct node *a = node->args[i];
        if (a)
            node_free(a);
        else
            break;
    }

    if (node->cap)
        capreq_free(node->cap);

    free(node);
}

static int node_set_arg(struct node *node, struct node *arg)
{
    int i = 0;
    while (i < NARY_MAX && node->args[i] != NULL)
        i++;

    if (i >= node->ary)
        return -1;

    n_assert(i < NARY_MAX);
    node->args[i] = arg;

    return i;
}

static inline const char *skipwhite(const char *p)
{
    while (isspace(*p))
        p++;
    return p;
}

static inline const char *skipnonwhite(const char *p)
{
    int bl = 0;
    while (*p && !(*p == ' ' || *p == ',' || (*p == ')' && bl-- <= 0)))
        if (*p++ == '(')
            bl++;
    return p;
}

static const char *parse_relflags(const char *p, unsigned *relflags) {
    *relflags = 0;
    while (*p) {
        if (*p == '<')
            *relflags |= REL_LT;
        else if (*p == '=')
            *relflags |= REL_EQ;
        else if (*p == '>')
            *relflags |= REL_GT;
        else
            break;
        p++;
    }
    return p;
}

/*
   Based on parseRichDep() from libsolv (BSD license)
   Copyright (c) 2015, SUSE Inc.

*/
static
struct node *parse_dep(const char **dep, int chain_type,
                       struct node *chain_opnode, int indent)
{
    const char *q, *p = *dep;
    struct node *opnode = NULL, *node = NULL;
    int ctype = chain_opnode ? chain_opnode->type : 0;

    p = skipwhite(p);
    trace(indent, "PARSE [%s], chain_type=%d,%d\n", p, chain_type, ctype);

    if (!chain_type && *p++ != '(') {
        n_assert(0);
        return NULL;
    }

    p = skipwhite(p);
    if (*p == ')')
        return NULL;

    if (*p == '(') {
        node = parse_dep(&p, 0, NULL, indent+2);
        if (!node)
            return NULL;

    } else {
        q = p;
        p = skipnonwhite(p);
        int len = p - q;
        if (len == 0)
           return NULL;

        char *name = alloca(len + 1);
        n_strncpy(name, q, len + 1);

        unsigned relflags = 0;
        p = skipwhite(p);
        p = parse_relflags(p, &relflags);

        char *evr = NULL;
        if (relflags) {
            p = skipwhite(p);
            q = p;
            p = skipnonwhite(p);

            len = p - q;
            if (len == 0)
                return NULL;    /* EVR expected */

            evr = alloca(len + 1);
            n_strncpy(evr, q, len + 1);
        }
        node = node_new(0, 0, name, evr, relflags);
        trace(indent, "IDENTIFIER %s", capreq_snprintf_s(node->cap));
    }

    p = skipwhite(p);
    if (!*p) {
        trace(indent, "EOF");
        return NULL;
    }

    if (*p == ')') {
        *dep = p + 1;
        trace(indent, "EOE");
        return node;
    }

    q = p;
    while (*p && !isspace(*p))
        p++;

    struct op *op;
    int len = p - q;
    for (op = operators; op->name != NULL; op++) {
        if (len == op->len && strncmp(q, op->name, op->len) == 0)
            break;
    }

    if (op->name == NULL) {     /* unknown op */
        return NULL;
    }

    if (op->type == OP_ELSE && (ctype == OP_IF || ctype == OP_UNLESS)) {
        n_assert(chain_opnode);
        trace(indent, "USE PARENT OP=%s -> %s", op->name, chain_opnode->name);
        opnode = chain_opnode;
    } else {
        opnode = node_new(op->type, op->ary, op->name, NULL, 0);
    }

    int nth = node_set_arg(opnode, node);
    if (nth == -1) {
        trace(indent, "ARITY ERR OP=%s arg=%s", opnode->name, node->name);
        return NULL;
    }

    trace(indent, "OP=%s arg[%d]=%s", opnode->name, nth, node->name);

    int optype = op->type;
    if (optype == 0) {          /* op not found */
        trace(indent, "ERR at [%s] (op not found)", q);
        return NULL;
    }

    trace(indent, "_chain_type [%s], ct=%d,%d, ot=%d,%d", p, chain_type, ctype, optype, opnode->type);
    if (ctype != 0 && opnode->type != ctype) {
        trace(indent, "ERR chain %s, pt=%d != ot=%d", p, ctype, optype);
        return NULL;
    }

    if ((chain_type == OP_IF || chain_type == OP_UNLESS) && optype == OP_ELSE) {
        trace(indent, "_chain_type ELSE RESET");
        chain_type = 0;
    }

    if (chain_type && optype != chain_type) {
        trace(indent, "RET NULL (cannot chain different ops) chain %s, %d && %d != %d", p, chain_type, optype, chain_type);
        return NULL;
    }

    node = parse_dep(&p, optype, opnode, indent + 2);
    if (!node) {
        trace(indent, "RET NULL");
        return NULL;
    }

    if (opnode != node) {
        nth = node_set_arg(opnode, node);
        if (nth == -1) {
            trace(indent, "ARITY ERR OP=%s arg=%s", opnode->name, node->name);
            return NULL;
        }
        trace(indent, "OP=%s arg[%d]=%s", opnode->name, nth, node->name);
    } else {
        trace(indent, "SAME %s, opnode ary %d", opnode->name, opnode->ary);
    }

    *dep = p;

    return opnode;
}

struct booldep *booldep_parse(const char *expr)
{
    if (!expr || *expr != '(')
        return NULL;

    struct node *node = parse_dep(&expr, 0, 0, 0);
    if (node == NULL)
        return NULL;

    int len = strlen(expr);
    struct booldep *dep = n_malloc(sizeof(*dep) + len + 1);
    dep->node = node;
    n_strncpy(dep->expr, expr, len);

    return dep;
}

void booldep_free(struct booldep *dep)
{
    node_free(dep->node);
    n_cfree(&dep);
}
