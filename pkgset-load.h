/* $Id$ */
#ifndef POLDEK_PKGSET_LOAD
#define POLDEK_PKGSET_LOAD

int pkgset_load_dir(struct pkgset *ps, const char *dirpath);
int pkgset_load_rpmidx(struct pkgset *ps, const char *fpath);
int pkgset_load_txtidx(struct pkgset *ps, const char *dirpath);

#endif
