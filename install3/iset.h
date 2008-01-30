/* $Id$ */
#ifndef POLDEK_INSTALL3_ISET_H
#define POLDEK_INSTALL3_ISET_H

#include <stdint.h>
#include <trurl/narray.h>

#include "pkgmisc.h"
#include "pkgset.h"

struct pkg;
struct capreq;
struct iset;

struct iset *iset_new(void);
void iset_free(struct iset *iset);

inline
void iset_markf(struct iset *iset, struct pkg *pkg, unsigned mflag);
inline
int iset_ismarkedf(struct iset *iset, const struct pkg *pkg, unsigned mflag);

const struct pkgmark_set *iset_pms(struct iset *iset);
const tn_array *iset_packages(struct iset *iset);

/* return array sorted by package recno */
const tn_array *iset_packages_by_recno(struct iset *iset);
tn_array *iset_packages_in_install_order(struct iset *iset);


void iset_add(struct iset *iset, struct pkg *pkg, unsigned mflag);
int  iset_remove(struct iset *iset, struct pkg *pkg);

int iset_provides(struct iset *iset, const struct capreq *cap);
int iset_has_pkg(struct iset *iset, const struct pkg *pkg);
struct pkg *iset_has_kind_of_pkg(struct iset *iset, const struct pkg *pkg);

#endif    
