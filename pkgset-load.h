/* $Id$ */
#ifndef POLDEK_PKGSET_LOAD
#define POLDEK_PKGSET_LOAD

/* ldmethod  */
#define PKGSET_LD_NIL      0    /* guess                   */
#define PKGSET_LD_DIR      1    /* scan directory          */
#define PKGSET_LD_IDX      2    /* read index file         */
#define PKGSET_LD_HDL      3    /* read hdlist file        */

#define PKGSOURCE_NOAUTO     (1 << 0)
#define PKGSOURCE_NOAUTOUP   (1 << 1)
#define PKGSOURCE_VER_GPG    (1 << 2)
#define PKGSOURCE_VER_PGP    (1 << 3)

struct source {
    unsigned  flags;
    char      *source_name;
    char      *source_path;
    char      *pkg_prefix;
    int       ldmethod;         /* PKGSET_LD_* */
};

struct source *source_new(const char *path, const char *pkg_prefix);
void source_free(struct source *src);
int source_cmp(struct source *s1, struct source *s2);
int source_cmp_name(struct source *s1, struct source *s2);
int source_update(struct source *src);
int source_snprintf_flags(char *str, int size, struct source *src);


int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources);

#endif
