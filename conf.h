/* $Id$ */
/* ini-like config parsing module */
#ifndef POLDEK_CONF_H
#define POLDEK_CONF_H

#include <trurl/narray.h>
#include <trurl/nhash.h>

#define POLDEK_LDCONF_FOREIGN   (1 << 0) /* not a poldek file */
#define POLDEK_LDCONF_NOVRFY    POLDEK_LDCONF_FOREIGN /* legacy */
#define POLDEK_LDCONF_UPDATE    (1 << 1)
#define POLDEK_LDCONF_NOINCLUDE (1 << 2) /* ignore %include directives */

tn_hash *poldek_conf_load(const char *path, unsigned flags);
tn_hash *poldek_conf_loadefault(unsigned flags);

tn_array *poldek_conf_get_section_arr(const tn_hash *htconf, const char *name);
tn_hash *poldek_conf_get_section_ht(const tn_hash *htconf, const char *name);

const char *poldek_conf_get(const tn_hash *htconf, const char *name, int *is_multi);
int poldek_conf_get_bool(const tn_hash *htconf, const char *name, int default_v);
int poldek_conf_get_int(const tn_hash *htconf, const char *name, int default_v);
tn_array *poldek_conf_get_multi(const tn_hash *htconf, const char *name);

#endif

