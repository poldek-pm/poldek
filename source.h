/* $Id$ */
#ifndef POLDEK_SOURCE_H
#define POLDEK_SOURCE_H

/* source types (ldmethod)  */
#define PKGSRCT_NIL      0            /* guess                  */
#define PKGSRCT_DIR      (1 << 0)     /* scan directory         */
#define PKGSRCT_IDX      (1 << 2)     /* native index file      */
#define PKGSRCT_HDL      (1 << 3)     /* hdlist                 */

/* source options */
#define PKGSOURCE_NOAUTO     (1 << 0)
#define PKGSOURCE_NOAUTOUP   (1 << 1)
#define PKGSOURCE_VRFY_GPG   (1 << 2)
#define PKGSOURCE_VRFY_PGP   (1 << 3)
#define PKGSOURCE_TYPE       (1 << 4)

#define PKGSOURCE_ISNAMED    (1 << 10)

struct source {
    unsigned  type;            /* PKGSRCT_* too */
    unsigned  flags;
    unsigned  subopt_flags;     /* PKGSRCT_*  */
    
    char      *name;            /* source name */
    char      *path;            /* path to idx */
    char      *pkg_prefix;      /* packages prefix path */
};

struct source *source_new(const char *path, const char *pkg_prefix);
void source_free(struct source *src);
int source_cmp(struct source *s1, struct source *s2);
int source_cmp_name(struct source *s1, struct source *s2);

#define PKGSOURCE_UP      (1 << 0)
#define PKGSOURCE_UPA     (1 << 1)
#define PKGSOURCE_UPAA    (1 << 2)

int source_update(struct source *src, unsigned flags);

void source_printf(const struct source *src);

#define source_idstr(src) \
 (((src)->flags & PKGSOURCE_ISNAMED) ? (src)->name : vf_url_slim_s((src)->path, 0))

#define source_is_remote(src) \
    (vf_url_type((src)->path) & VFURL_REMOTE)

#define source_is_local(src)  (!source_is_remote(src))


int sources_update(tn_array *sources, unsigned flags);

#define PKGSOURCE_CLEAN  (1 << 0)
#define PKGSOURCE_CLEANA (1 << 1)

int sources_clean(tn_array *sources, unsigned flags);

#endif
