/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POCLIDEK_DENT_H
#define  POCLIDEK_DENT_H

#include <trurl/narray.h>

#ifndef EXPORT
# define EXPORT extern
#endif

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
    
    const char *name;
    char      _buf[0];
};

#define	pkg_dent_ents ent.ents
#define	pkg_dent_pkg  ent.pkg

#define pkg_dent_isdir(ent) (ent->flags & PKG_DENT_DIR)

EXPORT struct pkg_dent *pkg_dent_link(struct pkg_dent *ent);

EXPORT void pkg_dent_free(struct pkg_dent *ent);

EXPORT struct pkg_dent *pkg_dent_add_dir(struct poclidek_ctx *cctx,
                                  struct pkg_dent *parent, const char *name);

EXPORT int pkg_dent_add_pkgs(struct poclidek_ctx *cctx,
                      struct pkg_dent *dent, tn_array *pkgs);

EXPORT struct pkg_dent *pkg_dent_add_pkg(struct poclidek_ctx *cctx,
                                  struct pkg_dent *dent, struct pkg *pkg);

EXPORT void pkg_dent_remove_pkg(struct pkg_dent *dent, struct pkg *pkg);


EXPORT int pkg_dent_cmp(struct pkg_dent *ent1, struct pkg_dent *ent2);
EXPORT int pkg_dent_cmp_btime(struct pkg_dent *ent1, struct pkg_dent *ent2);
EXPORT int pkg_dent_cmp_bday(struct pkg_dent *ent1, struct pkg_dent *ent2);
EXPORT int pkg_dent_strncmp(struct pkg_dent *ent, const char *name);

//void pkg_dent_sort(struct pkg_dent *ent,
//                   int (*cmpf)(struct pkg_dent *, struct pkg_dent*));


EXPORT int poclidek_chdir(struct poclidek_ctx *cctx, const char *path);

EXPORT char *poclidek_dent_dirpath(char *path, int size, const struct pkg_dent *dent);
EXPORT tn_array *poclidek_get_dent_ents(struct poclidek_ctx *cctx, const char *dir);
EXPORT tn_array *poclidek_get_dent_packages(struct poclidek_ctx *cctx, const char *dir);

EXPORT struct pkg_dent *poclidek_dent_root(struct poclidek_ctx *cctx);
EXPORT struct pkg_dent *poclidek_dent_find(struct poclidek_ctx *cctx, const char *path);
EXPORT struct pkg_dent *poclidek_dent_ldfind(struct poclidek_ctx *cctx, const char *path);

EXPORT tn_array *poclidek_resolve_dents(const char *path, struct poclidek_ctx *cctx,
                                 struct poldek_ts *ts, int flags);

EXPORT tn_array *poclidek_resolve_packages(const char *path, struct poclidek_ctx *cctx,
                                    struct poldek_ts *ts, int flags);

EXPORT char *poclidek_pwd(struct poclidek_ctx *cctx, char *path, int size);

#ifndef SWIG
EXPORT struct pkg_dent *poclidek_dent_setup(struct poclidek_ctx *cctx,
                                     const char *path, tn_array *pkgs,
                                     int force);
#endif

#endif
