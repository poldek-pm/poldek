/* $Id$ */
#ifndef  POCLIDEK_DENT_H
#define  POCLIDEK_DENT_H

#include <trurl/narray.h>
struct poclidek_ctx;

#define PKG_DENT_DIR     (1 << 0)
#define PKG_DENT_DELETED (1 << 1)

struct pkg_dent {
    uint16_t         _refcnt;
    uint16_t         flags;
    struct pkg_dent  *parent;
    
    union {
        tn_array        *ents;
        struct pkg      *pkg;
    } ent;
    
    char      *name;
    char      _buf[0];
};

#define	pkg_dent_ents ent.ents
#define	pkg_dent_pkg  ent.pkg

#define pkg_dent_isdir(ent) (ent->flags & PKG_DENT_DIR)

struct pkg_dent *pkg_dent_link(struct pkg_dent *ent);

void pkg_dent_free(struct pkg_dent *ent);

struct pkg_dent *pkg_dent_adddir(struct poclidek_ctx *cctx,
                                 struct pkg_dent *dent, const char *name);

int pkg_dent_add_pkgs(struct poclidek_ctx *cctx,
                      struct pkg_dent *dent, tn_array *pkgs);

struct pkg_dent *pkg_dent_add_pkg(struct poclidek_ctx *cctx,
                                  struct pkg_dent *dent, struct pkg *pkg);

void pkg_dent_remove_pkg(struct pkg_dent *dent, struct pkg *pkg);


int pkg_dent_cmp(struct pkg_dent *ent1, struct pkg_dent *ent2);
int pkg_dent_cmp_btime(struct pkg_dent *ent1, struct pkg_dent *ent2);
int pkg_dent_cmp_bday(struct pkg_dent *ent1, struct pkg_dent *ent2);
int pkg_dent_strncmp(struct pkg_dent *ent, const char *name);

//void pkg_dent_sort(struct pkg_dent *ent,
//                   int (*cmpf)(struct pkg_dent *, struct pkg_dent*));


void poclidek_dent_init(struct poclidek_ctx *cctx);

int poclidek_chdir(struct poclidek_ctx *cctx, const char *path);

char *poclidek_dent_dirpath(char *path, int size, const struct pkg_dent *dent);
tn_array *poclidek_get_dent_ents(struct poclidek_ctx *cctx, const char *dir);
tn_array *poclidek_get_dent_packages(struct poclidek_ctx *cctx, const char *dir);

struct pkg_dent *poclidek_dent_find(struct poclidek_ctx *cctx, const char *path);

tn_array *poclidek_resolve_dents(const char *path, struct poclidek_ctx *cctx,
                                 struct poldek_ts *ts, int exact);

tn_array *poclidek_resolve_packages(const char *path, struct poclidek_ctx *cctx,
                                    struct poldek_ts *ts, int exact);

char *poclidek_pwd(struct poclidek_ctx *cctx, char *path, int size);

#endif
