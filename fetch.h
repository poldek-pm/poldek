/* $Id$ */
#ifndef POLDEK_FETCH_H
#define POLDEK_FETCH_H

#include <trurl/narray.h>

#define URL_PATH    (1 << 0)
#define URL_FTP     (1 << 1)
#define URL_HTTP    (1 << 2)
#define URL_HTTPS   (1 << 3)
#define URL_RSYNC   (1 << 4)

int url_type(const char *url);
char *url_as_dirpath(char *buf, size_t size, const char *url);
char *url_as_path(char *buf, size_t size, const char *url);


int fetch_register_handler(unsigned urltypes, char *fmt);

int fetch_file(const char *destdir, const char *url, int urltype);
int fetch_files(const char *destdir, tn_array *urls, int urltype);

#endif
