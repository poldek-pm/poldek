/* $Id$ */
/* ini-like config parsing module */
#ifndef POLDEK_CONF_H
#define POLDEK_CONF_H

#include <trurl/narray.h>
#include <trurl/nhash.h>

#define POLDEK_LDCONF_FOREIGN     (1 << 0) /* not a poldek config file */
#define POLDEK_LDCONF_NOVALIDATE  (1 << 1) /* do not validate config variables */
#define POLDEK_LDCONF_UPDATE      (1 << 2) /* resync with remote config */
#define POLDEK_LDCONF_NOINCLUDE   (1 << 3) /* ignore %include directives */
#define POLDEK_LDCONF_GLOBALONLY  (1 << 4) /* for early cachedir setup */

/* default localization is used if path is NULL */
tn_hash *poldek_conf_load(const char *path, unsigned flags);
#define poldek_conf_load_default(flags) poldek_conf_load(NULL, flags)
    
/*
  Adds to htconf parameters discovered from lines; htconf is created if NULL.
  Caution: parameters from lines overwrite previously discovered ones, i.e.
  if lines = [ 'foo = a', 'foo = b' ] then 'foo' value will be 'b'
*/
tn_hash *poldek_conf_addlines(tn_hash *htconf, const char *sectnam,
                              tn_array *lines);

tn_array *poldek_conf_get_sections(const tn_hash *htconf, const char *name);
tn_hash *poldek_conf_get_section(const tn_hash *htconf, const char *name);

const char *poldek_conf_get(const tn_hash *htconf, const char *name, int *is_multi);
int poldek_conf_get_bool(const tn_hash *htconf, const char *name, int default_v);
int poldek_conf_get_bool3(const tn_hash *htconf, const char *name, int default_v);
int poldek_conf_get_int(const tn_hash *htconf, const char *name, int default_v);
tn_array *poldek_conf_get_multi(const tn_hash *htconf, const char *name);


void *poldek_conf_add_section(tn_hash *htconf, const char *name);
int poldek_conf_add_to_section(void *sect, const char *key, const char *val);

int poldek_conf_set(tn_hash *ht_sect, const char *akey, const char *aval);

#endif

