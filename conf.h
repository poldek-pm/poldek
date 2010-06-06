/* $Id$ */
/* ini-like config parsing module */
#ifndef POLDEK_CONF_H
#define POLDEK_CONF_H

#include <trurl/narray.h>
#include <trurl/nhash.h>

#ifndef EXPORT
#  define EXPORT extern
#endif

#define POLDEK_LDCONF_FOREIGN     (1 << 0) /* not a poldek config file */
#define POLDEK_LDCONF_NOVALIDATE  (1 << 1) /* do not validate config variables */
#define POLDEK_LDCONF_UPDATE      (1 << 2) /* resync with remote config */
#define POLDEK_LDCONF_NOINCLUDE   (1 << 3) /* ignore %include directives */
#define POLDEK_LDCONF_GLOBALONLY  (1 << 4) /* for early cachedir setup */

/* default localization is used if path is NULL */
EXPORT tn_hash *poldek_conf_load(const char *path, unsigned flags);
#define poldek_conf_load_default(flags) poldek_conf_load(NULL, flags)
    
/*
  Adds to htconf parameters discovered from lines; htconf is created if NULL.
  Caution: parameters from lines overwrite previously discovered ones, i.e.
  if lines = [ 'foo = a', 'foo = b' ] then 'foo' value will be 'b'
*/
EXPORT tn_hash *poldek_conf_addlines(tn_hash *htconf, const char *sectnam,
                              tn_array *lines);

EXPORT tn_array *poldek_conf_get_sections(const tn_hash *htconf, const char *name);
EXPORT tn_hash *poldek_conf_get_section(const tn_hash *htconf, const char *name);

EXPORT const char *poldek_conf_get(const tn_hash *htconf, const char *name, int *is_multi);
EXPORT int poldek_conf_get_bool(const tn_hash *htconf, const char *name, int default_v);
EXPORT int poldek_conf_get_bool3(const tn_hash *htconf, const char *name, int default_v);
EXPORT int poldek_conf_get_int(const tn_hash *htconf, const char *name, int default_v);
EXPORT tn_array *poldek_conf_get_multi(const tn_hash *htconf, const char *name);


EXPORT void *poldek_conf_add_section(tn_hash *htconf, const char *name);
EXPORT int poldek_conf_add_to_section(void *sect, const char *key, const char *val);

EXPORT int poldek_conf_set(tn_hash *ht_sect, const char *akey, const char *aval);

#endif

