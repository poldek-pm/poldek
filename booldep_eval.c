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
#include <stdbool.h>

#include <trurl/trurl.h>
#include <trurl/nbuf.h>

#include "capreq.h"
#include "pkg.h"
#include "log.h"

#define POLDEK_BOOLDEP_INTERNAL
#include "booldep.h"

struct dvalue {
    struct capreq  *req;
    int            cost;
    tn_array       *providers;
    struct dvalue  *next;
};

static int dvalue_cost(struct dvalue *dv)
{
    if (dv == NULL || dv->req == NULL) {
        DBGF(" (null) RET cost 99\n");
        return 99;
    }

    int cost = 0;
    for (; dv != NULL; dv = dv->next) {
        DBGF("%s cost=%d\n", capreq_name(dv->req), dv->cost);
        cost += dv->cost < 0 ? 99 : dv->cost;
    }

    DBGF("  RET cost %d\n", cost);
    return cost;
}

static struct dvalue *dvalue_new(void)
{
    struct dvalue *dv = n_malloc(sizeof(*dv));
    memset(dv, 0, sizeof(*dv));

    return dv;
}

static void dvalue_free(struct dvalue *dv) {
    if (dv->next)
        dvalue_free(dv->next);

    if (dv->req)
        capreq_free(dv->req);

    n_array_cfree(&dv->providers);

    n_cfree(&dv);
}

static void dvalue_dump(const struct dvalue *dv, const char *label)
{
    if (poldek_TRACE == 0)
        return;

    if (dv == NULL) {
        trace(0, "%s is NULL\n", label);
        return;
    }

    char providers[4096];
    *providers = '\0';

    if (dv->providers) {
        int n = 0;

        for (int i=0; i < n_array_size(dv->providers); i++) {
            struct pkg *pkg = n_array_nth(dv->providers, i);
            n += n_snprintf(&providers[n], sizeof(providers) - n,
                            "%s,", pkg_id(pkg));
        }
        providers[n-1] = '\0';
    }

    trace(0, "%s %s cost=%d, providers: %s\n", label, capreq_stra(dv->req),
          dv->cost, providers);
}


static
struct dvalue *eval(struct node *node, const struct booldep_eval_ctx *ctx);

static struct dvalue *eval_req(struct node *node, const struct booldep_eval_ctx *ctx)
{
    n_assert(node->type == 0);
    struct dvalue *dv = dvalue_new();

    dv->req = capreq_clone(NULL, node->cap);
    dv->next = NULL;

    dv->cost = 0;
    if (ctx->req_cost) {
        dv->cost = ctx->req_cost(dv->req, &dv->providers, ctx->ctx);
    }

    return dv;
}

static struct dvalue *eval_and(struct node *node, const struct booldep_eval_ctx *ctx)
{
    struct dvalue *left = eval(node->args[0], ctx);
    struct dvalue *right = eval(node->args[1], ctx);

    struct dvalue *dv = left;
    while (dv->next)
        dv = dv->next;

    dv->next = right;

    return left;
}

static struct dvalue *eval_or(struct node *node, const struct booldep_eval_ctx *ctx)
{
    struct dvalue *left = eval(node->args[0], ctx);
    struct dvalue *right = eval(node->args[1], ctx);

    int lcost = dvalue_cost(left);
    int rcost = dvalue_cost(right);

    dvalue_dump(left, "or.left");
    dvalue_dump(right, "or.right");

    if (rcost < lcost) {
        dvalue_free(left);
        return right;
    }

    dvalue_free(right);

    /* defaults to left if both costs are equal */
    return left;
}

static struct dvalue *eval_if(struct node *node, const struct booldep_eval_ctx *ctx)
{
    struct dvalue *cond = eval(node->args[1], ctx);
    int cost = dvalue_cost(cond);
    dvalue_free(cond);

    if (cost == 0)
        return eval(node->args[0], ctx); /* then */

    return eval(node->args[2], ctx); /* else */
}

static struct dvalue *eval_unless(struct node *node, const struct booldep_eval_ctx *ctx)
{
    struct dvalue *cond = eval(node->args[1], ctx);
    int cost = dvalue_cost(cond);
    dvalue_free(cond);

    if (cost != 0)
        return eval(node->args[0], ctx); /* then */

    return eval(node->args[2], ctx); /* else */
}


static int pkg_eq_ptr(const struct pkg *p1, const struct pkg *p2)
{
    return p1 == p2 ? 0 : 1;
}

static struct capreq *take_best(tn_array *pkgs, const struct booldep_eval_ctx *ctx)
{
    if (n_array_size(pkgs) == 1) {
        struct pkg *pkg = n_array_nth(pkgs, 0);
        return capreq_new(NULL, pkg->name, pkg->epoch, pkg->ver,
                          pkg->rel, REL_EQ, CAPREQ_BASTARD);
    }

    struct capreq *best = NULL;
    int min_cost = 99;

    for (int i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        struct capreq *req = capreq_new(NULL, pkg->name, pkg->epoch, pkg->ver,
                                        pkg->rel, REL_EQ, CAPREQ_BASTARD);

        int cost = ctx->req_cost(req, NULL, ctx->ctx);

        tracef(0, "%s cost=%d\n", pkg_id(pkg), cost);

        if (cost >= 0 && cost < min_cost) {
            min_cost = cost;
            if (best)
                capreq_free(best);
            best = req;
        } else {
            capreq_free(req);
        }
    }

    return best;
}

static struct dvalue *eval_with(struct node *node, const struct booldep_eval_ctx *ctx)
{
    struct dvalue *left = NULL, *right = NULL;
    tn_array *re = NULL;

    left = eval(node->args[0], ctx);
    if (left == NULL || left->providers == NULL || n_array_size(left->providers) == 0)
        goto l_none;

    right = eval(node->args[1], ctx);
    if (right == NULL || right->providers == NULL || n_array_size(right->providers) == 0)
        goto l_none;

    dvalue_dump(left, "with.left");
    dvalue_dump(right, "with.right");

    re = pkgs_array_new(4);

    for (int i=0; i < n_array_size(left->providers); i++) {
        struct pkg *pkg = n_array_nth(left->providers, i);
        if (n_array_bsearch(right->providers, pkg))
            n_array_push(re, pkg_link(pkg));
    }

    if (n_array_size(re) == 0)
        goto l_none;

    dvalue_free(right);

    capreq_free(left->req);
    left->req = take_best(re, ctx);

    n_array_free(left->providers);
    left->providers = re;

    dvalue_dump(left, "with.RE");

    return left;

 l_none:
    if (left)
        dvalue_free(left);

    if (right)
        dvalue_free(right);

    if (re)
        n_array_free(re);

    return NULL;
}


static struct dvalue *eval_without(struct node *node, const struct booldep_eval_ctx *ctx)
{
    struct dvalue *left = NULL, *right = NULL;

    left = eval(node->args[0], ctx);
    if (left == NULL || left->providers == NULL || n_array_size(left->providers) == 0)
        goto l_none;

    right = eval(node->args[1], ctx);
    if (right == NULL || right->providers == NULL || n_array_size(right->providers) == 0)
        goto l_none;

    dvalue_dump(left, "without.left");
    dvalue_dump(right, "without.right");

    for (int i=0; i < n_array_size(right->providers); i++) {
        struct pkg *pkg = n_array_nth(right->providers, i);

        n_array_remove_ex(left->providers, pkg, (tn_fn_cmp)pkg_eq_ptr);
        if (n_array_size(left->providers) == 0)
            break;
    }

    if (n_array_size(left->providers) == 0) /* no packages fullfills both sides */
        goto l_none;

    capreq_free(left->req);
    left->req = take_best(left->providers, ctx);

    dvalue_dump(left, "without.RE");
    dvalue_free(right);

    return left;

 l_none:
    if (left)
        dvalue_free(left);

    if (right)
        dvalue_free(right);

    return NULL;
}

static
struct dvalue *eval(struct node *node, const struct booldep_eval_ctx *ctx)
{
    struct dvalue *re = NULL;

    if (node == NULL)
        return dvalue_new();    /* empty dvalue */

    switch (node->type) {
    case 0:
        re = eval_req(node, ctx);
        break;

    case OP_AND:
        re = eval_and(node, ctx);
        break;

    case OP_OR:
        re = eval_or(node, ctx);
        break;

    case OP_IF:
        re = eval_if(node, ctx);
        break;

    case OP_UNLESS:
        re = eval_unless(node, ctx);
        break;

    case OP_WITH:
        re = eval_with(node, ctx);
        break;

    case OP_WITHOUT:
        re = eval_without(node, ctx);
        break;

    default:
        break;
    }

    return re;
}

tn_array *booldep_eval(struct booldep *dep, const struct booldep_eval_ctx *ctx)
{
    struct dvalue *dv = eval(dep->node, ctx);

    if (dv == NULL)
        return NULL;

    tn_array *reqs = capreq_arr_new(4);
    for (struct dvalue *v = dv; v != NULL; v = v->next) {
        if (v->req)
            n_array_push(reqs, capreq_clone(NULL, v->req));
    }
    dvalue_free(dv);

    return reqs;
}
