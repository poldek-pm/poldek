/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/*!
  Package class
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/param.h>


#include <trurl/nstr.h>
#include <trurl/nassert.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "capreq.h"
#include "pkgfl.h"
#include "pkgu.h"
#include "pkg.h"
#include "pkgdir/pkgdir.h"
#include "pkgroup.h"
#include "pkgcmp.h"
#include "pkg_ver_cmp.h"
#include "thread.h"

int poldek_conf_PROMOTE_EPOCH = 0;
int poldek_conf_MULTILIB = 0;

static tn_hash *architecture_h = NULL;
static tn_array *architecture_a = NULL;

static tn_hash *operatingsystem_h = NULL;
static tn_array *operatingsystem_a = NULL;

struct an_arch {
    int score;
    int index;
    char arch[0];
};

#ifdef ENABLE_THREADS
static pthread_mutex_t arch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t os_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static struct an_arch *last_arch = NULL;

static
int pkgmod_register_arch(const char *arch)
{
    struct an_arch *an_arch;

    an_arch = __atomic_load_n(&last_arch, __ATOMIC_RELAXED);
    if (an_arch && strcmp(an_arch->arch, arch) == 0) {
        return an_arch->index;
    }

    mutex_lock(&arch_mutex);

    if (architecture_h == NULL) {
        architecture_h = n_hash_new(21, free);
        architecture_a = n_array_new(16, NULL, NULL);
        n_hash_ctl(architecture_h, TN_HASH_NOCPKEY);
    }

    if ((an_arch = n_hash_get(architecture_h, arch)) == NULL) {
        int len = strlen(arch);

        an_arch = n_malloc(sizeof(*an_arch) + len + 1);

        an_arch->score = pm_architecture_score(arch);
        n_assert(an_arch->score >= 0);
        if (an_arch->score == 0) {
            /* make it most less preferred, but differ from other zero-scored
               archs (i686 and x86_64 on x32 case) */
            an_arch->score = INT_MAX - n_array_size(architecture_a) - 1;
        }
        DBGF("register %s with score %d\n", arch, an_arch->score);

        memcpy(an_arch->arch, arch, len + 1);
        n_array_push(architecture_a, an_arch);

        /* +1 in fact; 0 means no arch */
        an_arch->index = n_array_size(architecture_a);
        n_assert(an_arch->index < UINT16_MAX);
        n_hash_insert(architecture_h, an_arch->arch, an_arch);

        __atomic_store_n(&last_arch, an_arch, __ATOMIC_RELAXED);
    }

    mutex_unlock(&arch_mutex);

    return an_arch->index;
}

const char *pkg_arch(const struct pkg *pkg)
{
    if (pkg->_arch) {
        struct an_arch *a = n_array_nth(architecture_a, pkg->_arch - 1);
        n_assert(a);
        return a->arch;
    }

    return NULL;
}

int pkg_arch_score(const struct pkg *pkg)
{
    struct an_arch *a;

    if (!pkg->_arch)
        return 0;

    a = n_array_nth(architecture_a, pkg->_arch - 1);
    return a->score;
}

int pkg_set_arch(struct pkg *pkg, const char *arch)
{
    pkg->_arch = pkgmod_register_arch(arch);
    return 1;
}

struct an_os {
    //int score;
    int index;
    char os[0];
};

static struct an_os *last_os = NULL;

static
int pkgmod_register_os(const char *os)
{
    struct an_os *an_os;

    an_os = __atomic_load_n(&last_os, __ATOMIC_RELAXED);
    if (an_os && strcmp(an_os->os, os) == 0) {
        return an_os->index;
    }

    mutex_lock(&os_mutex);

    if (operatingsystem_h == NULL) {
        operatingsystem_h = n_hash_new(21, free);
        operatingsystem_a = n_array_new(16, NULL, NULL);
        n_hash_ctl(operatingsystem_h, TN_HASH_NOCPKEY);
    }

    if ((an_os = n_hash_get(operatingsystem_h, os)) == NULL) {
        int len = strlen(os);

        an_os = n_malloc(sizeof(*an_os) + len + 1);

        //an_os->score = pm__score(os);
        //n_assert(an_os->score >= 0);
        //if (!an_os->score) an_os->score = INT_MAX - 1;

        memcpy(an_os->os, os, len + 1);
        n_array_push(operatingsystem_a, an_os);
        /* +1 in fact; 0 means no os */
        an_os->index = n_array_size(operatingsystem_a);
        n_hash_insert(operatingsystem_h, an_os->os, an_os);

        __atomic_store_n(&last_os, an_os, __ATOMIC_RELAXED);
    }

    mutex_unlock(&os_mutex);

    return an_os->index;
}

const char *pkg_os(const struct pkg *pkg)
{
    if (pkg->_os) {
        struct an_os *o = n_array_nth(operatingsystem_a, pkg->_os - 1);
        n_assert(o);
        return o->os;
    }
    return NULL;
}

int pkg_set_os(struct pkg *pkg, const char *os)
{
    pkg->_os = pkgmod_register_os(os);
    return 1;
}


/* always store fields in order: path, name, version, release, arch */
struct pkg *pkg_new_ext(tn_alloc *na,
                        const char *name, int32_t epoch,
                        const char *version, const char *release,
                        const char *arch, const char *os,
                        const char *fn, const char *srcfn,
                        uint32_t size, uint32_t fsize,
                        uint32_t btime)
{
    struct pkg *pkg;
    int name_len = 0, version_len = 0, release_len = 0, fn_len = 0,
        srcfn_len = 0, arch_len = 0;
    char *buf, pkg_fn[PATH_MAX], pkg_srcfn[PATH_MAX];
    uint32_t flags = 0;
    int len;

    n_assert(name);
    n_assert(version);
    n_assert(release);

    if (version == NULL || release == NULL)
        return NULL;

    name_len = strlen(name);
    len = name_len + 1;

    version_len = strlen(version);
    len += version_len + 1;

    release_len = strlen(release);
    len += release_len + 1;

    if (fn && arch) {           /* compare filename with "standard" name */
        //fn = n_basenam(fn);
        int n = n_snprintf(pkg_fn, sizeof(pkg_fn), "%s-%s-%s.%s.rpm", name,
                           version, release, arch);
        //printf("cmp %s %s\n", pkg_fn, fn);
        if (strncmp(pkg_fn, fn, n) == 0)
            fn = NULL;
        else {
            fn_len = strlen(fn);
            len += fn_len + 1;
        }
    }

    if (srcfn) {   /* compare source filename with "standard" name */
        flags |= PKG_HAS_SRCFN;
        if (strcmp(srcfn, "-") == 0) /* "default" */
            srcfn = NULL;

        else {
            int n = n_snprintf(pkg_srcfn, sizeof(pkg_srcfn), "%s-%s-%s.src.rpm", name,
                               version, release);

            if (strncmp(pkg_srcfn, srcfn, n) == 0)
                srcfn = NULL;
        }

        if (srcfn) { /* non default name, it's child package */
            char *p = strstr(srcfn, ".src.rpm");
            if (p)
                *p = '\0';

            srcfn_len = strlen(srcfn);
            len += srcfn_len + 1;
        }
    }

    len += len + 1;             /* for id (nvr) */

    if (poldek_conf_MULTILIB && arch) {
        arch_len = strlen(arch);
        len += arch_len + 1;
    }

    len += 1;
    if (na == NULL) {
        pkg = n_calloc(1, sizeof(*pkg) + len);

    } else {
        pkg = na->na_calloc(na, sizeof(*pkg) + len);
        pkg->na = n_ref(na);
        DBGF("+%p %p %d\n", na, &na->_refcnt, na->_refcnt);
    }
    pkg->flags = flags;
    pkg->epoch = epoch;
    pkg->size = size;
    pkg->fsize = fsize;
    pkg->btime = btime;
    pkg->_buf_size = len;
    buf = pkg->_buf;

    pkg->name = buf;
    memcpy(buf, name, name_len);
    buf += name_len;
    *buf++ = '\0';

    pkg->ver = buf;
    memcpy(buf, version, version_len);
    buf += version_len;
    *buf++ = '\0';

    pkg->rel = buf;
    memcpy(buf, release, release_len);
    buf += release_len;
    *buf++ = '\0';

    pkg->fn = NULL;
    if (fn) {
        pkg->fn = buf;
        memcpy(buf, fn, fn_len);
        buf += fn_len;
        *buf++ = '\0';
    }

    pkg->srcfn = NULL;
    if (srcfn) {
        pkg->srcfn = buf;
        memcpy(buf, srcfn, srcfn_len);
        buf += srcfn_len;
        *buf++ = '\0';
    }

    if (arch)
        pkg->_arch = pkgmod_register_arch(arch);

    if (os)
        pkg->_os = pkgmod_register_os(os);

    pkg->_nvr = buf;
    memcpy(buf, name, name_len);
    buf += name_len;
    *buf++ = '-';

    memcpy(buf, version, version_len);
    buf += version_len;
    *buf++ = '-';

    memcpy(buf, release, release_len);
    buf += release_len;


    if (poldek_conf_MULTILIB && arch) {
        *buf++ = '.';
        memcpy(buf, arch, arch_len);
        buf += arch_len;
    }

    *buf++ = '\0';
    pkg->reqs = NULL;
    pkg->caps = NULL;
    pkg->cnfls = NULL;
    pkg->sugs = NULL;
    pkg->revreqs = NULL;
    pkg->fl = NULL;

    pkg->pkgdir = NULL;
    pkg->pkgdir_data = NULL;
    pkg->pkgdir_data_free = NULL;
    pkg->load_pkguinf = NULL;
    pkg->load_nodep_fl = NULL;

    pkg->pri = 0;
    pkg->groupid = 0;
    pkg->_refcnt = 0;

    return pkg;
}

#if 0                           /* XXX: NFY */
static tn_array *clone_array(tn_array *arr, unsigned flags)
{
    flags = flags;
    if (arr)
        return n_ref(arr);
    return NULL;
}

struct pkg *pkg_clone(tn_alloc *na, struct pkg *pkg, unsigned flags)
{
    struct pkg *new;

    new = pkg_new_ext(na, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                      pkg_arch(pkg), pkg_os(pkg), pkg->fn, pkg->srcfn,
                      pkg->size, pkg->fsize, pkg->btime);

    new->fmtime = pkg->fmtime;
    n_assert(flags & PKG_LDNEVR); /* deep copy not implemented yet */

    new->caps  = clone_array(pkg->caps, flags);
    new->reqs  = clone_array(pkg->reqs, flags);
    new->cnfls = clone_array(pkg->cnfls, flags);
    new->sugs = clone_array(pkg->sugs, flags);
    new->revreqs = clone_array(pkg->revreqs, flags);

    if (pkg->fl)
        new->fl = n_ref(pkg->fl)
    new->reqpkgs = clone_array(pkg->reqpkgs, flags);
    new->revreqpkgs = clone_array(pkg->revreqpkgs, flags);
    new->cnflpkgs = clone_array(pkg->cnflpkgs, flags);

    pkg->pkgdir = NULL;
    pkg->pkgdir_data = NULL;
    pkg->pkgdir_data_free = NULL;
    pkg->load_pkguinf = NULL;
    pkg->load_nodep_fl = NULL;

    pkg->pri = 0;
    pkg->groupid = 0;           /* remapping not implemented */
    pkg->_refcnt = 0;

    return pkg;
}
#endif

#if TRACE_PACKAGE
#include <execinfo.h>
void dump_backtrace(void)
{
  void *array[10];
  char **strings;
  int size, i;

  size = backtrace(array, 10);
  strings = backtrace_symbols(array, size);
  if (strings != NULL)  {
      for (i = 1; i < size; i++)
          printf("  %s\n", strings[i]);
  }

  free(strings);
  fflush(stdout);
}

static struct pkg *__trace_pkg = NULL;
#endif  /* TRACE_PACKAGE */

void pkg_free(struct pkg *pkg)
{
#if TRACE_PACKAGE
    if (pkg == __trace_pkg) {
        printf("pkg_free %p %s %d\n", pkg, pkg_id(pkg), pkg->_refcnt);
        dump_backtrace();
    }
#endif

    if (pkg->_refcnt > 0) {
        pkg->_refcnt--;
        return;
    }
    n_array_cfree(&pkg->caps);
    n_array_cfree(&pkg->reqs);
    n_array_cfree(&pkg->cnfls);
    n_array_cfree(&pkg->sugs);
    n_array_cfree(&pkg->revreqs);

    if (pkg->fl) {
        n_tuple_free(pkg->na, pkg->fl);
        pkg->fl = NULL;
    }

    if (pkg_has_ldpkguinf(pkg)) {
        if (pkg->pkg_pkguinf)
            pkguinf_free(pkg->pkg_pkguinf);
        pkg_clr_ldpkguinf(pkg);
    }

    if (pkg->pkgdir_data && pkg->pkgdir_data_free) {
        pkg->pkgdir_data_free(pkg->na, pkg->pkgdir_data);
        pkg->pkgdir_data = NULL;
    }


    if (pkg->na) {
        tn_alloc *na = pkg->na;
        memset(pkg, 0, sizeof(*pkg));
        DBGF("-%p %p %d\n", na, &na->_refcnt, na->_refcnt);
        n_alloc_free(na);
        return;
    }

    memset(pkg, 0, sizeof(*pkg));
    n_free(pkg);
}


int pkg_add_selfcap(struct pkg *pkg)
{
    struct capreq *selfcap;
    int i, has = 0;

    if (pkg->flags & PKG_HAS_SELFCAP)
        return 1;

    if (pkg->caps == NULL) {
        pkg->caps = capreq_arr_new(0);

    } else if ((i = capreq_arr_find(pkg->caps, pkg->name)) != -1) {
        for (; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);

            if (n_str_ne(capreq_name(cap), pkg->name))
                break;

            if (pkg_eq_capreq(pkg, cap)) {
                has = 1;
                break;
            }
        }
    }

    pkg->flags |= PKG_HAS_SELFCAP;

    if (has == 1) {
        n_array_uniq(pkg->caps);
        return 1;
    }

    selfcap = capreq_new(pkg->na, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                         REL_EQ, 0);

    n_array_push(pkg->caps, selfcap);
    n_array_isort(pkg->caps);
    n_array_uniq(pkg->caps);

    return 1;
}

/* RET: bool, true if cmprc matches relation */
__inline__ static
int rel_match(int cmprc, const struct capreq *req)
{
    if (cmprc == 0)
        cmprc = req->cr_relflags & REL_EQ;
    else if (cmprc > 0)
        cmprc = req->cr_relflags & REL_GT;
    else if (cmprc < 0)
        cmprc = req->cr_relflags & REL_LT;
    else
        n_assert(0);

    return cmprc;
}

#define rel_not_match(cmprc, req) (rel_match(cmprc, req) == 0)

static void promote_epoch_warn(int verbose_level,
                               const char *title0, const char *p0,
                               const char *p1)
{
    if (poldek_VERBOSE > verbose_level)
        logn(LOGWARN, "%s '%s' needs an epoch (assuming same "
             "epoch as %s)\n", title0, p0, p1);
}


int cap_xmatch_req(const struct capreq *cap, const struct capreq *req,
                   unsigned flags)
{
    register int cmprc = 0, evr = 0;

    DBGF("cap %s req %s\n", capreq_snprintf_s(cap), capreq_snprintf_s0(req));

    if (strcmp(capreq_name(cap), capreq_name(req)) != 0)
        return 0;

    if (!capreq_versioned(req))
        return 1;

    if (capreq_has_epoch(cap) || capreq_has_epoch(req)) {
        int do_promote = 0;

        if (poldek_conf_PROMOTE_EPOCH)
            flags |= POLDEK_MA_PROMOTE_EPOCH;

        if (flags & POLDEK_MA_PROMOTE_EPOCH) {
            if (!capreq_has_epoch(req) && (flags & POLDEK_MA_PROMOTE_REQEPOCH)){
                promote_epoch_warn(1, "req", capreq_snprintf_s(req),
                                   capreq_snprintf_s0(cap));
                do_promote = 1;
            }

            if (!capreq_has_epoch(cap) && (flags & POLDEK_MA_PROMOTE_CAPEPOCH)){
                promote_epoch_warn(1, "cap", capreq_snprintf_s(cap),
                                   capreq_snprintf_s0(req));
                do_promote = 1;
            }
        }

        if (do_promote) {
            cmprc = 0;

        } else {
            cmprc = capreq_epoch(cap) - capreq_epoch(req);
            if (cmprc != 0)
                return rel_match(cmprc, req);
        }
        evr = 1;

    }
#if 0                           /* disabled autopromotion */
    else if (capreq_epoch(req) > 0) { /* always promote cap's epoch */
        cmprc = 0;
        evr = 1;
    }
#endif

#if 0
    if (capreq_has_epoch(req)) {
        if (!capreq_has_epoch(cap))
            return strict == 0;

        cmprc = capreq_epoch(cap) - capreq_epoch(req);
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }
#endif

    if (capreq_has_ver(req)) {
        if (!capreq_has_ver(cap))
            return (flags & POLDEK_MA_PROMOTE_VERSION);

        cmprc = pkg_version_compare(capreq_ver(cap), capreq_ver(req));
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }

    if (capreq_has_rel(req)) {
        if (!capreq_has_rel(cap))
            return (flags & POLDEK_MA_PROMOTE_VERSION);

        cmprc = pkg_version_compare(capreq_rel(cap), capreq_rel(req));
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }

    return evr ? rel_match(cmprc, req) : 1;
}

int cap_match_req(const struct capreq *cap, const struct capreq *req,
                  bool strict)
{
    return cap_xmatch_req(cap, req, strict ? 0 : POLDEK_MA_PROMOTE_VERSION);
}


static inline
int do_pkg_evr_match_req(const struct capreq *pkgcap, const struct capreq *req,
                         int promote_epoch)
{
    unsigned ma_flags = 0;

    if (promote_epoch == -1)
        promote_epoch = poldek_conf_PROMOTE_EPOCH;

    if (promote_epoch)     /* rpm promotes Requires epoch only */
        ma_flags |= POLDEK_MA_PROMOTE_REQEPOCH;

    return cap_xmatch_req(pkgcap, req, ma_flags);
}

int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req, unsigned flags)
{
    struct capreq *cap;
    register int rc = 0;

    n_assert(strcmp(pkg->name, capreq_name(req)) == 0);

    if (!capreq_versioned(req))
        return 1;

    cap = capreq_new(NULL, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                     REL_EQ, 0);

    if (flags & (POLDEK_MA_PROMOTE_VERSION | POLDEK_MA_PROMOTE_EPOCH)) {
        rc = do_pkg_evr_match_req(cap, req, 0) ? 1 : 0;
        if (!rc && pkg->epoch)  /* promote the epoch */
            rc = do_pkg_evr_match_req(cap, req, 1) ? 1 : 0;

    } else {
        rc = do_pkg_evr_match_req(cap, req, -1) ? 1 : 0;
    }

    capreq_free(cap);

    DBGF("%s match %s ? %s\n", pkg_evr_snprintf_s(pkg),
         capreq_snprintf_s(req), rc ? "YES" : "NO");
    return rc;
}


/* look up into package caps only */
int pkg_caps_match_req(const struct pkg *pkg, const struct capreq *req,
                       unsigned flags)
{
    int n;

    DBGF("\npkg_caps_match_req %s %s\n", pkg_snprintf_s(pkg),
           capreq_snprintf_s(req));

    if (pkg->caps == NULL || n_array_size(pkg->caps) == 0)
        return 0;     /* not match */

    if ((n = capreq_arr_find(pkg->caps, capreq_name(req))) == -1) {
        return 0;

    } else {
        int i;
        for (i = n; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);

            /* names not equal -> return with false;
               eq test omitting for first cap */
            if (i > n && n_str_ne(capreq_name(cap), capreq_name(req))) {
                DBGF("  cap[%d] %s -> NOT match, IRET\n", i,
                       capreq_snprintf_s(cap));
                return 0;
            }

            if (cap_xmatch_req(cap, req, flags)) {
                DBGF("  cap[%d] %s -> match (pkg: %s)\n", i, capreq_snprintf_s(cap), pkg->name);
                return 1;
            }

            DBGF("  cap[%d] %s -> NOT match\n", i, capreq_snprintf_s(cap));
        }
    }

    return 0;
}


static int pkg_has_path_dirindex(const struct pkg *pkg,
                                 const char *dirname, const char *basename)
{
    char path[PATH_MAX];

    if (pkg->pkgdir == NULL || pkg->pkgdir->dirindex == NULL)
        return 0;

    n_snprintf(path, sizeof(path), "%s%s/%s", *dirname != '/' ? "/" : "",
               dirname, basename);

    return pkgdir_dirindex_pkg_has_path(pkg->pkgdir, pkg, path);
}

int pkg_has_path(const struct pkg *pkg,
                 const char *dirname, const char *basename)
{
    struct pkgfl_ent *flent, tmp;
    int rc = 0;

    if (pkg->fl == NULL || n_tuple_size(pkg->fl) == 0)
        return pkg_has_path_dirindex(pkg, dirname, basename);

    if (*dirname == '/' && *(dirname + 1) != '\0')
        dirname++;

    tmp.dirname = (char*)dirname;
    tmp.items = 0;

    flent = n_tuple_bsearch_ex(pkg->fl, &tmp, (tn_fn_cmp)pkgfl_ent_cmp);
    if (flent != NULL) {
        int i;

        for (i=0; i<flent->items; i++) {
            if (strcmp(basename, flent->files[i]->basename) == 0) {
                rc = 1;
                break;
            }
        }
    }

    if (rc == 0)
        rc = pkg_has_path_dirindex(pkg, dirname, basename);

    return rc;
}


int pkg_xmatch_req(const struct pkg *pkg, const struct capreq *req, unsigned flags)
{
    if (n_str_eq(pkg->name, capreq_name(req))) {
        if (pkg_evr_match_req(pkg, req, flags))
            return 1;
    }

    return pkg_caps_match_req(pkg, req, flags);
}


int pkg_match_req(const struct pkg *pkg, const struct capreq *req, bool strict)
{
    if (strcmp(pkg->name, capreq_name(req)) == 0 &&
        pkg_evr_match_req(pkg, req, strict ? 0 : POLDEK_MA_PROMOTE_VERSION))
        return 1;

    return pkg_caps_match_req(pkg, req, strict ? 0 : POLDEK_MA_PROMOTE_VERSION);
}


int pkg_satisfies_req(const struct pkg *pkg, const struct capreq *req,
                      bool strict)
{
    if (!capreq_is_file(req))
        return pkg_match_req(pkg, req, strict);

    else {
        char *dirname, *basename, path[PATH_MAX];

        strncpy(path, capreq_name(req), sizeof(path));

        path[PATH_MAX - 1] = '\0';
        n_basedirnam(path, &dirname, &basename);
        n_assert(dirname);

        if (*dirname == '\0')   /* path = "/foo" */
            dirname = "/";

        n_assert(*dirname);
        if (*dirname == '/' && *(dirname + 1) != '\0')
            dirname++;

        return pkg_has_path(pkg, dirname, basename);
    }

    return 1;
}

static
struct capreq *find_req(const struct pkg *pkg, const struct capreq *cap)
{
    if (pkg->reqs == NULL)
        return NULL;

    n_array_sort(pkg->reqs);

    int nth = n_array_bsearch_idx_ex(pkg->reqs, cap, (tn_fn_cmp)capreq_cmp_name);
    if (nth == -1)
        return NULL;

    for (int i = nth; i < n_array_size(pkg->reqs); i++) {
        struct capreq *req = n_array_nth(pkg->reqs, i);
        DBGF("%d. %s\n", i, capreq_stra(req));
    }

    struct capreq *rreq = n_array_nth(pkg->reqs, nth);
    for (int i = nth+1; i < n_array_size(pkg->reqs); i++) {
        struct capreq *req = n_array_nth(pkg->reqs, i);

        if (strcmp(capreq_name(req), capreq_name(cap)) != 0)
            break;

        int m = cap_match_req(req, rreq, 1);
        DBGF("%s match %s => %d\n", capreq_stra(rreq), capreq_stra(req), m);

        if (m) {
            rreq = req;
        }
    }
    DBGF("RET %s\n", capreq_stra(rreq));

    return rreq;
}

const struct capreq *pkg_requires_cap(const struct pkg *pkg,
                                      const struct capreq *cap)
{
    DBGF("%s requires %s (reqs=%p, size=%d)?\n", pkg_id(pkg),
         capreq_snprintf_s(cap), pkg->reqs,
         pkg->reqs ? n_array_size(pkg->reqs) : 0);

    if (pkg->reqs == NULL)
        return NULL;

    struct capreq *rreq = find_req(pkg, cap);
    if (rreq == NULL) {
        DBGF("%s not found => null\n", capreq_stra(cap));
        return NULL;
    }

    if (!capreq_versioned(cap)) {
        DBGF("%s is not versioned => %s\n", capreq_stra(cap), capreq_stra(rreq));
        return rreq;
    }

    if (!capreq_versioned(rreq)) {
        DBGF("%s: non-versioned req => null\n", capreq_stra(rreq));
        return NULL;
    }

    n_assert(capreq_versioned(cap) && capreq_versioned(rreq));

    int m = cap_match_req(cap, rreq, 1);
    DBGF("cap_match_req(%s, %s) => %d\n", capreq_stra(cap), capreq_stra(rreq), m);
    if (!m) {
        DBGF("%s not matches %s => null\n", capreq_stra(cap), capreq_stra(rreq));
        rreq = NULL;
    }

    DBGF("ret %s\n", rreq ? capreq_stra(rreq) : NULL);
    return rreq;
}

#if 0
const struct capreq *old_pkg_requires_cap(const struct pkg *pkg,
                                          const struct capreq *cap)
{
    struct capreq *rreq = NULL;
    int i;


    DBGF("%s requires %s (reqs=%p, size=%d)?\n", pkg_id(pkg),
         capreq_snprintf_s(cap), pkg->reqs,
         pkg->reqs ? n_array_size(pkg->reqs) : 0);

    if (pkg->reqs == NULL)
        return NULL;

    n_array_sort(pkg->reqs);
    i = n_array_bsearch_idx_ex(pkg->reqs, cap, (tn_fn_cmp)capreq_cmp_name);
    if (i == -1)
        return NULL;

    while (i < n_array_size(pkg->reqs)) {
        struct capreq *req = n_array_nth(pkg->reqs, i);
        int matched = 0;

        i++;

        if (strcmp(capreq_name(req), capreq_name(cap)) != 0)
            break;

        if (!capreq_versioned(cap)) {
            rreq = req;
            break;
        }

        if (!capreq_versioned(req))
            continue;

        matched = cap_match_req(cap, req, 1);
        DBGF("  cap_match_req %s %s => %d\n", capreq_snprintf_s(cap),
             capreq_snprintf_s0(req), matched);

        if (matched) {
            rreq = req;
            break;
        }
    }

    return rreq;
}
#endif

int pkg_obsoletes_pkg(const struct pkg *pkg, const struct pkg *opkg)
{
    if (strcmp(pkg->name, opkg->name) != 0)
        return pkg_caps_obsoletes_pkg_caps(pkg, opkg);

    return pkg_cmp_evr(pkg, opkg) > 0;
}


/* look up into package caps */
int pkg_caps_obsoletes_pkg_caps(const struct pkg *pkg, const struct pkg *opkg)
{
    int n;

    DBG("\npkg_obs_match_pkg %s %s\n", pkg_id(pkg), pkg_id(opkg));

    if (pkg->cnfls == NULL || n_array_size(pkg->cnfls) == 0)
        return 0;     /* not match */

    if ((n = capreq_arr_find(pkg->cnfls, opkg->name)) == -1) {
        return 0;
    }

    struct capreq *cnfl;

    cnfl = n_array_nth(pkg->cnfls, n);

    if (capreq_is_obsl(cnfl) && pkg_match_req(opkg, cnfl, 1)) {
        DBG("chk%d (%s) -> match\n", n, capreq_snprintf_s(cnfl));
        return 1;
    }
    n++;

    for (int i = n; i<n_array_size(pkg->cnfls); i++) {
        cnfl = n_array_nth(pkg->cnfls, n);
        if (!capreq_is_obsl(cnfl))
            continue;

        if (strcmp(capreq_name(cnfl), pkg->name) != 0) {
            DBG("chk%d %s -> NOT match IRET\n", i, capreq_snprintf_s(cnfl));
            return 0;
        }


        if (pkg_match_req(opkg, cnfl, 1)) {
            DBG("chk %s -> match\n", capreq_snprintf_s(cnfl));
            return 1;
        } else {
            DBG("chk%d %s -> NOT match\n", i, capreq_snprintf_s(cnfl));
        }
    }
    DBG("NONE\n");

    return 0;
}

int pkg_add_pkgcnfl(struct pkg *pkg, struct pkg *cpkg, int isbastard)
{
    struct capreq *cnfl = NULL;

    DBGF("%s %s%s", pkg_id(pkg), pkg_id(cpkg), isbastard ? " (bastard)" : "");
    if (!capreq_arr_contains(pkg->cnfls, cpkg->name)) {
        cnfl = capreq_new(pkg->na, cpkg->name, cpkg->epoch, cpkg->ver,
                          cpkg->rel, REL_EQ,
                          (isbastard ? CAPREQ_BASTARD : 0));

        n_array_push(pkg->cnfls, cnfl);
        n_array_sort(pkg->cnfls);
    }

    return cnfl != NULL;
}

int pkg_has_pkgcnfl(struct pkg *pkg, struct pkg *cpkg)
{
    return pkg->cnfls && capreq_arr_contains(pkg->cnfls, cpkg->name);
}

struct pkguinf *pkg_xuinf(const struct pkg *pkg, tn_array *langs)
{
    struct pkguinf *pkgu = NULL;

    if (pkg->load_pkguinf)
        pkgu = pkg->load_pkguinf(NULL, pkg, pkg->pkgdir_data, langs);

    else if (pkg_has_ldpkguinf(pkg))
        pkgu = pkguinf_link(pkg->pkg_pkguinf);

    return pkgu;
}

struct pkguinf *pkg_uinf(const struct pkg *pkg)
{
    struct pkguinf *pkgu = NULL;
    if (pkg_has_ldpkguinf(pkg))
        pkgu = pkguinf_link(pkg->pkg_pkguinf);

    else if (pkg->load_pkguinf)
        pkgu = pkg->load_pkguinf(NULL, pkg, pkg->pkgdir_data, NULL);

    return pkgu;
}

tn_array *pkg_required_dirs(const struct pkg *pkg)
{
    if (pkg->pkgdir && pkg->pkgdir->dirindex)
        return pkgdir_dirindex_get_required(pkg->pkgdir, pkg);

    if (pkg->fl) {
        tn_array *owned = NULL, *required = NULL;
        pkgfl_owned_and_required_dirs(pkg->fl, &owned, &required);
        n_array_cfree(&owned);
        return required;
    }

    return NULL;
}

tn_array *pkg_owned_dirs(const struct pkg *pkg)
{
    if (pkg->pkgdir && pkg->pkgdir->dirindex)
        return pkgdir_dirindex_get_provided(pkg->pkgdir, pkg);

    if (pkg->fl) {
        tn_array *owned = NULL, *required = NULL;
        pkgfl_owned_and_required_dirs(pkg->fl, &owned, &required);
        n_array_cfree(&required);
        return owned;
    }
    return NULL;
}

static tn_tuple *do_pkg_other_fl(tn_alloc *na, const struct pkg *pkg)
{
    tn_tuple *fl = NULL;

    if (pkg->load_nodep_fl)
        fl = pkg->load_nodep_fl(na, pkg,
                                pkg->pkgdir_data,
                                pkg->pkgdir ?
                                pkg->pkgdir->foreign_depdirs : NULL);

    return fl;
}

struct pkgflist *pkg_get_nodep_flist(const struct pkg *pkg)
{
    struct pkgflist *flist = NULL;
    tn_tuple *fl = NULL;
    tn_alloc *na;

    na = n_alloc_new(16, TN_ALLOC_OBSTACK);
    fl = do_pkg_other_fl(na, pkg);
    if (fl) {
        flist = na->na_malloc(na, sizeof(*flist));
        flist->_na = na;
        flist->fl = fl;
        n_tuple_sort_ex(flist->fl, (tn_fn_cmp)pkgfl_ent_cmp);
    }

    return flist;
}


struct pkgflist *pkg_get_flist(const struct pkg *pkg)
{
    struct pkgflist *flist = NULL;
    tn_tuple *fl = NULL;
    tn_alloc *na;

    na = n_alloc_new(16, TN_ALLOC_OBSTACK);

    if (!pkg_has_ldallfiles(pkg))
        fl = do_pkg_other_fl(na, pkg);

    if (pkg->fl == NULL) {
        if (fl == NULL) {
            n_alloc_free(na);
        } else {
            flist = na->na_malloc(na, sizeof(*flist));
            flist->_na = na;
            flist->fl = fl;
        }

    } else {                    /* pkg->fl != NULL  */
        if (fl == NULL) {
            n_alloc_free(na);
            flist = n_malloc(sizeof(*flist));
            flist->_na = NULL;
            flist->fl = pkg->fl;

        } else {
            tn_tuple *wholefl;
            int i, n;

            wholefl = n_tuple_new(na, n_tuple_size(pkg->fl) + n_tuple_size(fl),
                                  NULL);
            n = 0;
            for (i=0; i<n_tuple_size(pkg->fl); i++)
                n_tuple_set_nth(wholefl, n++, n_tuple_nth(pkg->fl, i));

            for (i=0; i<n_tuple_size(fl); i++)
                n_tuple_set_nth(wholefl, n++, n_tuple_nth(fl, i));

            flist = na->na_malloc(na, sizeof(*flist));
            flist->_na = na;
            flist->fl = wholefl;
        }
    }

    if (flist)
        n_tuple_sort_ex(flist->fl, (tn_fn_cmp)pkgfl_ent_cmp);

    DBGF("RET %p, fl = %p, na = %p\n", flist, flist ? flist->fl : NULL,
           flist ? flist->_na : NULL);

    return flist;
}

void pkgflist_free(struct pkgflist *flist)
{
    DBGF("FRE %p, fl = %p, na = %p\n", flist, flist ? flist->fl : NULL,
           flist ? flist->_na : NULL);

    if (flist->_na)
        n_alloc_free(flist->_na);
    else
        free(flist);
}

struct pkgflist_it {
    struct pkgflist *flist;
    struct pkgfl_it *_it;
};

static struct pkgflist_it *pkgflist_it_new(struct pkgflist *flist)
{
    struct pkgflist_it *it = n_malloc(sizeof(*it));
    it->flist = flist;
    it->_it = pkgfl_it_new(flist->fl);
    return it;
}

void pkgflist_it_free(struct pkgflist_it *it)
{
    pkgflist_free(it->flist);
    free(it->_it);
    free(it);
}

const char *pkgflist_it_get(struct pkgflist_it *it, struct flfile **flfile)
{
    return pkgfl_it_get(it->_it, flfile);
}

const char *pkgflist_it_get_rawargs(struct pkgflist_it *it, uint32_t *size,
                                    uint16_t *mode, const char **basename)
{
    return pkgfl_it_get_rawargs(it->_it, size, mode, basename);
}

struct pkgflist_it *pkg_get_flist_it(const struct pkg *pkg)
{
    struct pkgflist *flist = pkg_get_flist(pkg);
    if (flist)
        return pkgflist_it_new(flist);
    return NULL;
}

const char *pkg_group(const struct pkg *pkg)
{
    if (pkg->pkgdir && pkg->pkgdir->pkgroups)
        return pkgroup(pkg->pkgdir->pkgroups, pkg->groupid);
    return NULL;
}

char *pkg_srcfilename(const struct pkg *pkg, char *buf, size_t size)
{
    int n_len, v_len, r_len;
    unsigned len = 0;
    char *s;

    if (pkg->srcfn) {
        n_snprintf(buf, size, "%s.src.rpm", pkg->srcfn);
        return buf;
    }

    if ((pkg->flags & PKG_HAS_SRCFN) == 0)
        return NULL;

    n_len = pkg->ver - pkg->name - 1;
    v_len = pkg->rel - pkg->ver - 1;
    r_len = strlen(pkg->rel);

    len = n_len + 1 + v_len + 1 +
        r_len + 1 + 3/* src */ + 1/* '.' */ + 3/* "rpm" */ + 1;

    if (len >= size)
        return NULL;

    s = buf;
    /* all pkg members are stored in _buf */
    memcpy(s, pkg->name, len - 4 - 3);

    s += n_len;
    n_assert(*s == '\0');
    *s++ = '-';

    s += v_len;
    n_assert(*s == '\0');
    *s++ = '-';

    s += r_len;
    n_assert(*s == '\0');
    *s++ = '.';

    memcpy(s, "src", 3);
    s += 3;
    *s++ = '.';

    memcpy(s, "rpm\0", 4);
    return buf;
}

char *pkg_filename(const struct pkg *pkg, char *buf, size_t size)
{
    int n_len, v_len, r_len, a_len = 0;
    unsigned len = 0;
    const char *arch = NULL;
    char *s;


    if (pkg->fn) {
        n_snprintf(buf, size, pkg->fn);
        return buf;
    }

    n_len = pkg->ver  - pkg->name - 1;
    v_len = pkg->rel  - pkg->ver - 1;
    r_len = strlen(pkg->rel);

    if (pkg->_arch) {
        arch  = pkg_arch(pkg);
        a_len = strlen(arch);
    }

    len = n_len + 1 + v_len + 1 +
        r_len + 1 + a_len + 1/* '.' */ + 3/* "rpm" */ + 1;

    if (len >= size)
        return NULL;

    s = buf;
    /* all pkg members are stored in _buf */
    memcpy(s, pkg->name, len - 4 - a_len);

    s += n_len;
    n_assert(*s == '\0');
    *s++ = '-';

    s += v_len;
    n_assert(*s == '\0');
    *s++ = '-';

    s += r_len;
    n_assert(*s == '\0');
    *s++ = '.';

    if (arch) {
        memcpy(s, arch, a_len);
        s += a_len;
        //n_assert(*s == '\0');
        *s++ = '.';
    }

    memcpy(s, "rpm\0", 4);
    return buf;
}

char *pkg_path(const struct pkg *pkg, char *buf, size_t size)
{
    int n = 0;

    if (pkg->pkgdir->path) {
        n = n_snprintf(buf, size, "%s", pkg->pkgdir->path);
        if (n > 0 && buf[n - 1] != '/' && (size_t)n < size - 1) {
            buf[n++] = '/';
            buf[n] = '\0';
        }
    }

    if (pkg_filename(pkg, buf + n, size - n) == NULL)
        buf = NULL;

    return buf;
}

const char *pkg_pkgdirpath(const struct pkg *pkg)
{
    if (pkg->pkgdir)
        return pkg->pkgdir->path;
    return NULL;
}

unsigned pkg_file_url_type(const struct pkg *pkg)
{
    n_assert(pkg->pkgdir);
    return vf_url_type(pkg->pkgdir->path);
}

char *pkg_localpath(const struct pkg *pkg, char *path, size_t size,
                    const char *cachedir)
{
    char buf[1024], namebuf[1024], *fn;
    int n = 0;

    n_assert(pkg->pkgdir);

    if (vf_url_type(pkg->pkgdir->path) == VFURL_PATH)
        return pkg_path(pkg, path, size);

    fn = pkg_filename(pkg, namebuf, sizeof(namebuf));

    vf_url_as_dirpath(buf, sizeof(buf), pkg->pkgdir->path);
    n = n_snprintf(path, size, "%s%s%s/%s", cachedir ? cachedir : "",
                   cachedir ? "/" : "", buf, n_basenam(fn));

    DBGF("RET %s\n", path);
    if (size - n > 2)
        return path;

    return NULL;
}

int pkg_printf(const struct pkg *pkg, const char *str)
{
    return printf("%s-%s-%s%s", pkg->name, pkg->ver, pkg->rel,
           str ? str : "");
}

int pkg_evr_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    char e[32];

    *e = '\0';
    if (pkg->epoch)
        snprintf(e, sizeof(e), "%d:", pkg->epoch);

    return n_snprintf(str, size, "%s-%s%s-%s", pkg->name, e, pkg->ver, pkg->rel);
}

char *pkg_evr_str(char *str, size_t size, const struct pkg *pkg)
{
    if (pkg_evr_snprintf(str, size, pkg) > 0)
        return str;
    return NULL;
}

int pkg_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    int n;

    n = n_snprintf(str, size, "%s-%s-%s", pkg->name, pkg->ver, pkg->rel);
    return n;
}

char *pkg_str(char *str, size_t size, const struct pkg *pkg)
{
    if (pkg_snprintf(str, size, pkg) > 0)
        return str;
    return NULL;
}

tn_array *pkgs_array_new_ex(int size,
                            int (*cmpfn)(const struct pkg *p1,
                                         const struct pkg *p2))
{
    tn_array *arr;

    if (cmpfn == NULL)
        cmpfn = pkg_cmp_name_evr_rev;

    arr = n_array_new(size, (tn_fn_free)pkg_free, (tn_fn_cmp)cmpfn);
    n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    return arr;
}

void pkgs_array_dump(tn_array *pkgs, const char *prefix)
{
    int i;

    if (prefix == NULL)
        prefix = "array";

    msg(0, "%s = [ ", prefix);
    for (i=0; i<n_array_size(pkgs); i++)
        msg(0, "%s%s", pkg_id(n_array_nth(pkgs, i)),
            i + 1 == n_array_size(pkgs) ? " ":", ");
    msgn(0, "]");
}

tn_buf *pkgs_array_join(tn_array *arr, tn_buf *nbuf, const char *sep)
{
    int i, size = n_array_size(arr);

    if (sep == NULL)
        sep = ", ";

    if (nbuf == NULL)
        nbuf = n_buf_new(64 * n_array_size(arr));

    for (i=0; i < size; i++) {
        n_buf_printf(nbuf, "%s%s", pkg_id(n_array_nth(arr, i)),
                     i < size - 1 ? sep  : "");
    }
    return nbuf;
}

tn_array *pkgs_array_new(int size)
{
    return pkgs_array_new_ex(size, NULL);
}

char *pkg_strsize(char *buf, int size, const struct pkg *pkg)
{
    char unit = 'K';
    double pkgsize = pkg->size/1024;

    if (pkgsize >= 1024) {
        pkgsize /= 1024;
        unit = 'M';
    }

    n_snprintf(buf, size, "%.1f %cB", pkgsize, unit);
    return buf;
}

static
char *do_strtime(char *buf, int size, uint32_t time)
{
    time_t t = time;

    if (time)
        strftime(buf, size, "%Y/%m/%d %H:%M", localtime(&t));
    else
        *buf = '\0';

    buf[size-1] = '\0';
    return buf;
}


char *pkg_strbtime(char *buf, int size, const struct pkg *pkg)
{
    return do_strtime(buf, size, pkg->btime);
}

char *pkg_stritime(char *buf, int size, const struct pkg *pkg)
{
    return do_strtime(buf, size, pkg->itime);
}

void *pkg_na_malloc(struct pkg *pkg, size_t size)
{
    if (pkg->na)
        return pkg->na->na_malloc(pkg->na, size);
    n_assert(0);
    return NULL;
}

struct pkg *pkg_link(struct pkg *pkg)
{
#if TRACE_PACKAGE
    if (n_str_eq(pkg_id(pkg), "a-3-1.noarch")) {
        __trace_pkg = pkg;
        printf("pkg_link %p %s %d\n", pkg, pkg_id(pkg), pkg->_refcnt);
        dump_backtrace();
    }
#endif

    n_assert(pkg->_refcnt < UINT16_MAX - 1);
    pkg->_refcnt++;
    return pkg;
}

int pkg_id_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    return n_snprintf(str, size, "%s", pkg_id(pkg));
}

int pkg_idevr_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    if (poldek_conf_MULTILIB == 0)
        return n_snprintf(str, size, "%s-%s", pkg->ver, pkg->rel);

    return n_snprintf(str, size, "%s-%s.%s", pkg->ver, pkg->rel,
                      pkg_arch(pkg));
}
