/* $Id$ */
#ifndef POLDEK_USRSET_H
#define POLDEK_USRSET_H

#include <stdint.h>
#include <trurl/narray.h>

struct pkg;
struct arg_packages;
struct pkgset;
struct pm_ctx;

struct arg_packages *arg_packages_new(struct pkgset *ps, struct pm_ctx *ctx);
void arg_packages_free(struct arg_packages *aps);

void arg_packages_clean(struct arg_packages *aps);
tn_array *arg_packages_get_pkgmasks(struct arg_packages *aps);
int arg_packages_size(struct arg_packages *aps);

int arg_packages_add_pkgmask(struct arg_packages *aps, const char *mask);
int arg_packages_add_pkgmaska(struct arg_packages *aps, tn_array *masks);
int arg_packages_add_pkgfile(struct arg_packages *aps, const char *pathname);
int arg_packages_add_pkglist(struct arg_packages *aps, const char *path);
int arg_packages_add_pkg(struct arg_packages *aps, struct pkg *pkg);
int arg_packages_add_pkga(struct arg_packages *aps, tn_array *pkgs);

int arg_packages_setup(struct arg_packages *aps);

#define ARG_PACKAGES_RESOLV_EXACT          (1 << 0)
#define ARG_PACKAGES_RESOLV_MISSINGOK      (1 << 1) 
#define ARG_PACKAGES_RESOLV_UNAMBIGUOUS    (1 << 2)
#define ARG_PACKAGES_RESOLV_CAPS           (1 << 3)

tn_array *arg_packages_resolve(struct arg_packages *aps,
                               tn_array *avpkgs, unsigned flags);


#endif
