/* $Id$ */
#ifndef POLDEK_CONF_H
#define POLDEK_CONF_H

#include <trurl/narray.h>
#include <trurl/nhash.h>

tn_hash *poldek_ldconf(const char *path);
tn_hash *poldek_ldconf_default(void);

tn_array *poldek_conf_get_section_arr(const tn_hash *htconf, const char *name);
tn_hash *poldek_conf_get_section_ht(const tn_hash *htconf, const char *name);

char *poldek_conf_get(const tn_hash *htconf, const char *name, int *is_multi);
int poldek_conf_get_bool(const tn_hash *htconf, const char *name, int default_v);
int poldek_conf_get_int(const tn_hash *htconf, const char *name, int default_v);
tn_array *poldek_conf_get_multi(const tn_hash *htconf, const char *name);

#endif

