#ifndef PKGDIR_DIRINDEX_H
#define PKGDIR_DIRINDEX_H
/* Directory index */

#include <trurl/narray.h>

struct pkgdir;
struct pkgdir_dirindex;

struct pkgdir_dirindex *pkgdir__dirindex_open(struct pkgdir *pkgdir);
void pkgdir__dirindex_close(struct pkgdir_dirindex *dirindex);

/* returns path providers */
tn_array *pkgdir_dirindex_get(const struct pkgdir *pkgdir,
                              tn_array *pkgs, const char *path);
/* path belongs to pkg? */
int pkgdir_dirindex_pkg_has_path(const struct pkgdir *pkgdir,
                                 const struct pkg *pkg, const char *path);

/* returns directories required by package */
tn_array *pkgdir_dirindex_get_required(const struct pkgdir *pkgdir,
                                       const struct pkg *pkg);

/* for public prototypes see pkgdir.h */
#endif
