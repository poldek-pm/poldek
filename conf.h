/* $Id$ */
#ifndef POLDEK_CONF_H
#define POLDEK_CONF_H

#include <trurl/narray.h>

tn_hash *ldconf(const char *path);
tn_hash *ldconf_deafult(void);

char *conf_get(tn_hash *htconf, const char *name, int *is_multi);
tn_array *conf_get_multi(tn_hash *htconf, const char *name);

#endif

