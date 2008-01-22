/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <trurl/nstr.h>
#include <trurl/nassert.h>

#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "capreq.h"
#include "pkg.h"
#include "pkgfl.h"

/* caps, mean caps + owned files iterator */
struct pkg_cap_iter {
    struct pkg       *pkg;
    struct capreq    *cap;
    int              ncap;
    struct pkgfl_it  fit;
    int              fit_initialized;
};


struct pkg_cap_iter *pkg_cap_iter_new(struct pkg *pkg) 
{
    struct pkg_cap_iter *it = n_calloc(sizeof(*it), 1);
    it->pkg = pkg;
    it->ncap = 0;
    it->cap = NULL;
    it->fit_initialized = 0;
    return it;
}

void pkg_cap_iter_free(struct pkg_cap_iter *it) 
{
    if (it->cap)
        capreq_free(it->cap);
    memset(it, 0, sizeof(*it));
}

const struct capreq *pkg_cap_iter_get(struct pkg_cap_iter *it)
{
    struct capreq *cap;
    const char *path;
    struct flfile *flfile = NULL;
    
    if (it->pkg->caps && it->ncap < n_array_size(it->pkg->caps)) {
        cap = n_array_nth(it->pkg->caps, it->ncap);
        it->ncap++;
        return cap;
    }

    if (it->cap) {
        capreq_free(it->cap);
        it->cap = NULL;
    }
    
    if (it->pkg->fl == NULL)
        return NULL;

    if (!it->fit_initialized) {
        pkgfl_it_init(&it->fit, it->pkg->fl);
        it->fit_initialized = 1;
    }
    
    path = pkgfl_it_get(&it->fit, &flfile);
    if (path) {
        unsigned flags = CAPREQ_BASTARD;
        if (S_ISDIR(flfile->mode))
            flags |= CAPREQ_ISDIR;
        
        it->cap = capreq_new(NULL, path, 0, NULL, NULL, 0, flags);
    }
    return it->cap;
}


/* requirements iterator */
struct pkg_req_iter {
    const struct pkg       *pkg;
    unsigned         flags;

    int              nreq;
    int              nsug;

    tn_array         *requiredirs;
    struct capreq    *req;
    int              ndir;
};

struct pkg_req_iter *pkg_req_iter_new(const struct pkg *pkg, unsigned flags) 
{
    struct pkg_req_iter *it = n_calloc(sizeof(*it), 1);
    it->pkg = pkg;

    if (flags == 0)
        flags = PKG_ITER_REQIN;

    it->flags = flags;
    it->req = NULL;
    it->requiredirs = NULL;
    return it;
}

void pkg_req_iter_free(struct pkg_req_iter *it) 
{
    if (it->req)
        capreq_free(it->req);
    n_array_cfree(&it->requiredirs);
    memset(it, 0, sizeof(*it));
}

const struct capreq *pkg_req_iter_get(struct pkg_req_iter *it)
{
    struct capreq *req;
    
    if (it->pkg->reqs && it->nreq < n_array_size(it->pkg->reqs)) {
        req = n_array_nth(it->pkg->reqs, it->nreq);
        it->nreq++;

        if ((it->flags & PKG_ITER_REQUN) && !capreq_is_prereq_un(req))
            return pkg_req_iter_get(it);

        else if ((it->flags & PKG_ITER_REQUN) == 0 && capreq_is_prereq_un(req))
            return pkg_req_iter_get(it);
        
        return req;
    }

    if ((it->flags & PKG_ITER_REQSUG)) {
        if (it->pkg->sugs && it->nsug < n_array_size(it->pkg->sugs)) {
            req = n_array_nth(it->pkg->sugs, it->nsug);
            it->nsug++;

            return req;
        }
    }

    if ((it->flags & PKG_ITER_REQDIR) == 0) /* without required directories */
        return NULL;

    /* already got dirs in pkg->reqs */
    if (it->pkg->flags & PKG_INCLUDED_DIRREQS)
        return NULL;
    
    if (it->req) {
        capreq_free(it->req);
        it->req = NULL;
    }

    if (it->requiredirs == NULL) {
        it->requiredirs = pkg_required_dirs(it->pkg);
        if (it->requiredirs == NULL)
            return NULL;
        it->ndir = 0;
    }

    if (it->ndir < n_array_size(it->requiredirs)) {
        const char *path = n_array_nth(it->requiredirs, it->ndir);
        char tmp[PATH_MAX];

        n_snprintf(tmp, sizeof(tmp), "/%s", path);
        
        it->ndir++;
        it->req = capreq_new(NULL, tmp, 0, NULL, NULL, 0,
                             CAPREQ_BASTARD | CAPREQ_ISDIR);
        return it->req;
    }
    
    return NULL;
}

