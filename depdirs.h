/* $Id$ */
#ifndef POLDEK_DEPDIRS_H
#define POLDEK_DEPDIRS_H

#include <trurl/narray.h>

void init_depdirs(tn_array *dirnames);
void destroy_depdirs(void);

int in_depdirs(const char *dir);
int in_depdirs_l(const char *dir, int dirlen);

char *path2depdir(char *path);

#endif /* POLDEK_DEPDIRS_H */
