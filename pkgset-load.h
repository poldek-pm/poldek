/* $Id$ */
#ifndef POLDEK_PKGSET_LOAD
#define POLDEK_PKGSET_LOAD

/* ldmethod  */
#define PKGSET_LD_DIR      1    /* scan directory          */
#define PKGSET_LD_IDX      2    /* read Packages file      */

struct source {
    char      *source_path;
    char      *pkg_prefix;
    int       ldmethod;
};

struct source *source_new(const char *path, const char *pkg_prefix);
void source_free(struct source *src);
int source_cmp(struct source *s1, struct source *s2);
int source_update(struct source *src);
int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources);

#endif
