/* $Id$ */
#ifndef POLDEK_INSTALL_H
#define POLDEK_INSTALL_H

#include "pkgset.h"

int install_dist(struct pkgset *ps, struct inst_s *inst);
int upgrade_dist(struct pkgset *ps, struct inst_s *inst);
int install_pkgs(struct pkgset *ps, struct inst_s *inst, tn_array *unist_pkgs);

#endif
