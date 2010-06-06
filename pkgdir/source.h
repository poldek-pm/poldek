/* $Id$ */
#ifndef POLDEK_SOURCE_H
#define POLDEK_SOURCE_H

#include <trurl/narray.h>
#include <trurl/nhash.h>

#ifndef EXPORT
#  define EXPORT extern
#endif

EXPORT const char source_TYPE_GROUP[]; /* "group" */

/* source options */
#define PKGSOURCE_NOAUTO     (1 << 0)
#define PKGSOURCE_NOAUTOUP   (1 << 1)
#define PKGSOURCE_VRFY_GPG   (1 << 2)
#define PKGSOURCE_VRFY_PGP   (1 << 3)
#define PKGSOURCE_VRFY_SIGN  (1 << 4)
#define PKGSOURCE_TYPE       (1 << 5)
#define PKGSOURCE_PRI        (1 << 6)
#define PKGSOURCE_DSCR       (1 << 7)
#define PKGSOURCE_NAMED      (1 << 10)
#define PKGSOURCE_COMPRESS   (1 << 11)
#define PKGSOURCE_NODESC     (1 << 12)
#define PKGSOURCE_AUTOUPA    (1 << 13) /* do --upa if --up said "desynchronized"
                                          index */
#define PKGSOURCE_ISGROUP    (1 << 15) /* an alias for one or more sources */

struct source {
    unsigned  flags;
    
    char      *type;            /* type (as pkgdir types) */
    char      *name;            /* source name */
    char      *path;            /* path to idx */
    char      *pkg_prefix;      /* packages prefix path */
    
    char      *compress;        /* none, gz, bz2, etc */
    int       pri;
    int       no;
    char      *dscr;
    char      *lc_lang;
    tn_array  *exclude_path;
    tn_array  *ign_patterns;    /* ignore package patterns */
    char      *original_type;   /* type of source repo for this source  */
    unsigned  subopt_flags;
    int       _refcnt;
};

EXPORT struct source *source_malloc(void);

EXPORT struct source *source_new(const char *name, const char *type,
                          const char *path, const char *pkg_prefix);
EXPORT struct source *source_new_pathspec(const char *type, const char *pathspec,
                                   const char *pkg_prefix);
EXPORT struct source *source_new_htcnf(const tn_hash *htcnf);

#ifndef SWIG
/* sets type to v0.18.x default */
EXPORT struct source *source_new_v0_18(const char *pathspec, const char *pkg_prefix);
EXPORT struct source *source_clone(const struct source *src);
#endif
EXPORT void source_free(struct source *src);

EXPORT struct source *source_link(struct source *src);
EXPORT struct source *source_set_pkg_prefix(struct source *src, const char *prefix);
EXPORT struct source *source_set_type(struct source *src, const char *type);
EXPORT struct source *source_set_default_type(struct source *src);

EXPORT int source_cmp(const struct source *s1, const struct source *s2);
EXPORT int source_cmp_uniq(const struct source *s1, const struct source *s2);
EXPORT int source_cmp_name(const struct source *s1, const struct source *s2);
EXPORT int source_cmp_pri(const struct source *s1, const struct source *s2);
EXPORT int source_cmp_pri_name(const struct source *s1, const struct source *s2);
EXPORT int source_cmp_no(const struct source *s1, const struct source *s2);

#define PKGSOURCE_UP         (1 << 0)
#define PKGSOURCE_UPA        (1 << 1)
#define PKGSOURCE_UPAUTOA    (1 << 2)

EXPORT int source_update(struct source *src, unsigned flags);

EXPORT void source_printf(const struct source *src);

#define source_idstr(src) \
(((src)->flags & PKGSOURCE_NAMED) ? (src)->name : vf_url_slim_s((src)->path, 0))

#define source_is_remote(src) \
    (vf_url_type((src)->path) & VFURL_REMOTE)

#define source_is_local(src)  (!source_is_remote(src))

#define source_is_type(src, t) (strcmp((src)->type, t) == 0)

EXPORT int sources_update(tn_array *sources, unsigned flags);

#define PKGSOURCE_CLEAN      (1 << 0)
#define PKGSOURCE_CLEANPKG   (1 << 1)
#define PKGSOURCE_CLEANA     (PKGSOURCE_CLEAN | PKGSOURCE_CLEANPKG)
#define PKGSOURCE_CLEAN_TEST (1 << 3)
EXPORT int source_clean(struct source *src, unsigned flags);

EXPORT int sources_clean(tn_array *sources, unsigned flags);

/* flags = PKGDIR_CREAT_* */
EXPORT int source_make_idx(struct source *src, const char *stype,
                    const char *dtype, const char *idxpath,
                    unsigned flags, tn_hash *kw);

EXPORT int source_make_merged_idx(tn_array *sources,
                           const char *dtype, const char *idxpath,
                           unsigned flags, tn_hash *kw);


EXPORT int sources_add(tn_array *sources, struct source *src);
EXPORT void sources_score(tn_array *sources);

#endif
