/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POLDEK_PKGDIR_TNDB_H
#define  POLDEK_PKGDIR_TNDB_H

#include <sys/param.h>          /* for PATH_MAX */

#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <tndb/tndb.h>

#include <vfile/vfile.h>

#include "misc.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"

#define FILEFMT_MAJOR 1
#define FILEFMT_MINOR 0

/* packages.dir digest */
#define TNIDX_DIGEST_SIZE DIGEST_SIZE_SHA1

#define PNDIR_COMPRLEVEL 3

#define PNDIGEST_BRANDNEW (1 << 0) /* no diffs for index */
struct pndir_digest {
    struct vfile  *vf;
    unsigned      flags;
    char          compr[8];
    char          type[16];     /* sha1 so far */
    char          md[TNIDX_DIGEST_SIZE + 1];
};

struct pndir {
    struct vfile         *_vf;
    unsigned             crflags;
    struct tndb          *db;
    tn_hash              *db_dscr_h;
    char                 idxpath[PATH_MAX];
    struct pndir_digest  *dg;
    char                 *md_orig;
    char                 *srcnam; /* label for  */
    uint32_t             _tndb_first_pkg_nrec;
    uint32_t             _tndb_first_pkg_offs;
};

void pndir_init(struct pndir *idx);


char *pndir_mkidx_pathname(char *dest, size_t size, const char *pathname,
                           const char *suffix);

int pndir_tsstr(char *tss, int size, time_t ts);


extern const char *pndir_digest_ext;

int pndir_mkdigest_path(char *path, int size, const char *pathname,
                        const char *ext);

struct pndir_digest *pndir_digest_new(const char *path, int vfmode,
                                      const char *srcnam);
void pndir_digest_free(struct pndir_digest *pdg);

void pndir_digest_init(struct pndir_digest *pdg);
void pndir_digest_destroy(struct pndir_digest *pdg);

int pndir_digest_readfd(struct pndir_digest *pdg, int fd, const char *path);
//int pndir_digest_verify(struct pndir_digest *pdg, struct vfile *vf);

int pndir_digest_calc(struct pndir_digest *pdg, tn_array *keys);
int pndir_digest_calc_pkgs(struct pndir_digest *pdg, tn_array *pkgs);
int pndir_digest_save(struct pndir_digest *pdg, const char *pathname,
                      const struct pkgdir *pkgdir);

int pndir_make_pkgkey(char *key, size_t size, const struct pkg *pkg);
struct pkg *pndir_parse_pkgkey(char *key, int klen, struct pkg *pkg);

//static int pndir_m_open(struct pkgdir *pkgdir, unsigned flags);

int pndir_m_create(struct pkgdir *pkgdir, const char *pathname,
                   unsigned flags);

int pndir_m_update_a(const struct source *src, const char *idxpath,
                     enum pkgdir_uprc *uprc);
int pndir_m_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc);

const char *pndir_localidxpath(const struct pkgdir *pkgdir);


/* description.c */
extern
const char *pndir_db_dscr_idstr(const char *lang,
                                const char **idstr, const char **langstr);

tn_hash *pndir_db_dscr_h_new(void);
struct tndb *pndir_db_dscr_h_dbcreat(tn_hash *db_dscr_h, const char *pathtmpl,
                                     const char *lang);
int pndir_db_dscr_h_insert(tn_hash *db_dscr_h,
                           const char *lang, struct tndb *db);

struct tndb *pndir_db_dscr_h_get(tn_hash *db_dscr_h, const char *lang);

struct pkguinf *pndir_load_pkguinf(tn_alloc *na, tn_hash *db_dscr_h,
                                   const struct pkg *pkg, tn_array *langs);

#endif /* POLDEK_PKGDIR_H*/
