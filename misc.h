/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_MISC_H
#define POLDEK_MISC_H

#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <argp.h>

#include <vfile/p_open.h>
#include <trurl/narray.h>

int bin2hex(char *hex, int hex_size, const unsigned char *bin, int bin_size);


#define DIGEST_SHA1    1
#define DIGEST_MD5     2
#define DIGEST_SHA256  3

#define DIGEST_SIZE_MD5    32
#define DIGEST_SIZE_SHA1   40
#define DIGEST_SIZE_SHA256 64

int mhexdigest(FILE *stream, char *mdhex, int *mdhex_size, int type);

/*
  Returns $TMPDIR or "/tmp" if $TMPDIR isn't set.
  Returned dir always begin with '/'

*/

char *util__setup_cachedir(const char *path);

char *trimslash(char *path);
char *next_token(char **str, char delim, int *toklen);


int util__isdir(const char *path);
int util__mksubdir(const char *path, const char *dn);
int util__mkdir_p(const char *path, const char *dn);

char *util__abs_path(/*const */char *path);

void packages_display_summary(int verbose_l, const char *prefix, tn_array *pkgs,
                              int parseable);

int snprintf_size(char *buf, int bufsize, unsigned long nbytes,
                  int ndigits, int longunit);

tn_array *lc_lang_select(tn_array *avlangs, const char *lc_lang);


char *poldek__conf_path(char *s, char *v);

int poldek__is_in_testing_mode(void);

#include "poldek_util.h"
const char *lc_messages_lang(void);

void *timethis_begin(void);
void timethis_end(int verbose_level, void *tvp, const char *prefix);

char *strtime_(time_t t);

#endif /* POLDEK_MISC_H */
