/* $Id$ */
#ifndef POLDEK_USRSET_H
#define POLDEK_USRSET_H

#include <stdint.h>
#include <trurl/narray.h>
#include "pkg.h"


#define PKGDEF_REGNAME  (1 << 0) 
#define PKGDEF_PATTERN  (1 << 1)
#define PKGDEF_PKGFILE  (1 << 3)
#define PKGDEF_OPTIONAL (1 << 4) 
#define PKGDEF_VIRTUAL  (1 << 5)

struct pkgdef {
    struct pkg *pkg;
    uint8_t tflags;
    char virtname[0];
};

struct usrpkgset {
    tn_array *pkgdefs;         /* *pkgdef[] */
    char *path;
};

#define usrpkgset_size(ups)  n_array_size((ups)->pkgdefs)
struct usrpkgset *usrpkgset_new(void);
void usrpkgset_free(struct usrpkgset *ups);

int usrpkgset_add_str(struct usrpkgset *ups, char *def, int deflen);
int usrpkgset_add_file(struct usrpkgset *ups, const char *pathname);
int usrpkgset_add_list(struct usrpkgset *ups, const char *path);
int usrpkgset_add_list(struct usrpkgset *ups, const char *path);
int usrpkgset_add_pkg(struct usrpkgset *ups, struct pkg *pkg);
int usrpkgset_setup(struct usrpkgset *ups);

#endif
