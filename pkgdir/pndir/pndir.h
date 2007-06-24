/* $Id$ */
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
};

void pndir_init(struct pndir *idx);
void pndir_destroy(struct pndir *idx);

int pndir_open(struct pndir *idx, const char *path, int vfmode, unsigned flags,
               const char *srcnam);
void pndir_close(struct pndir *idx);


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

extern const char *pndir_packages_incdir;
extern const char *pndir_difftoc_suffix;
extern const char *pndir_extension;
extern const char *pndir_desc_suffix;

extern const char *pndir_poldeksindex;
extern const char *pndir_poldeksindex_toc;

extern const char *pndir_tag_hdr;
extern const char *pndir_tag_depdirs;
extern const char *pndir_tag_pkgroups;
extern const char *pndir_tag_langs;
extern const char *pndir_tag_ts;
extern const char *pndir_tag_ts_orig;
extern const char *pndir_tag_opt;
extern const char *pndir_tag_removed;
extern const char *pndir_tag_endhdr;


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
extern inline
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
