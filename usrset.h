/* $Id$ */
#ifndef POLDEK_USRSET_H
#define POLDEK_USRSET_H

#include <stdint.h>
#include <trurl/narray.h>
#include "pkg.h"

#define PKGDEF_OPTIONAL (1 << 0) 
#define PKGDEF_VIRTUAL  (1 << 1)

struct pkgdef {
    struct pkg *pkg;
    uint8_t tflags;
    char virtname[0];
};

#define pkgdef_is_virtual(pdef) (pdef->type == PKGSPEC_VIRTUAL)

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

int usrpkgset_setup(struct usrpkgset *ups);
#endif
