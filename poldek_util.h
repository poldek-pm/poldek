#ifndef POLDEK_UTIL_H

const char *poldek_util_lc_lang(const char *category);
int poldek_util_get_gmt_offs(void);
int poldek_util_is_rwxdir(const char *path);

const char *poldek_util_ngettext_n_packages_fmt(int n);
int poldek_util_parse_bool(const char *v);
#endif
