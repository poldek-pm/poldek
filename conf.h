/* $Id$ */
#ifndef POLDEK_LDCONF_H
#define POLDEK_LDCONF_H

#include <trurl/narray.h>
#include <trurl/nhash.h>

#define POLDEK_LDCONF_FOREIGN  (1 << 0) /*  */
#define POLDEK_LDCONF_NOVRFY   POLDEK_LDCONF_FOREIGN /* legacy */
#define POLDEK_LDCONF_UPDATE   (1 << 1) 

tn_hash *poldek_conf_load(const char *path, unsigned flags);
tn_hash *poldek_conf_loadefault(void);

tn_array *poldek_conf_get_section_arr(const tn_hash *htconf, const char *name);
tn_hash *poldek_conf_get_section_ht(const tn_hash *htconf, const char *name);

char *poldek_conf_get(const tn_hash *htconf, const char *name, int *is_multi);
int poldek_conf_get_bool(const tn_hash *htconf, const char *name, int default_v);
int poldek_conf_get_int(const tn_hash *htconf, const char *name, int default_v);
tn_array *poldek_conf_get_multi(const tn_hash *htconf, const char *name);

#endif

