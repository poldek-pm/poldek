/* $Id$ */
#ifndef POLDEK_PKGSET_LOAD
#define POLDEK_PKGSET_LOAD

/* source types (ldmethod)  */
#define PKGSRCT_NIL      0            /* guess                  */
#define PKGSRCT_DIR      (1 << 0)     /* scan directory         */
#define PKGSRCT_IDX      (1 << 2)     /* native index file      */
#define PKGSRCT_HDL      (1 << 3)     /* hdlist                 */


/* options */
#define PKGSOURCE_NOAUTO     (1 << 0)
#define PKGSOURCE_NOAUTOUP   (1 << 1)
#define PKGSOURCE_VER_GPG    (1 << 2)
#define PKGSOURCE_VER_PGP    (1 << 3)
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
int source_update(struct source *src);

void source_printf(const struct source *src);

#define source_idstr(src) \
        (((src)->flags & PKGSOURCE_ISNAMED) ? (src)->name : vf_url_slim_s((src)->path, 0))

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources);

#endif
