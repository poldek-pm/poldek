/* $Id$ */
#ifndef POLDEK_MISC_H
#define POLDEK_MISC_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <argp.h>

#include <vfile/p_open.h>
#include <trurl/narray.h>

const char *ngettext_n_packages_fmt(int n);
void translate_argp_options(struct argp_option *arr);

int bin2hex(char *hex, int hex_size, const unsigned char *bin, int bin_size);


#define DIGEST_SHA1 1
#define DIGEST_MD5  2

#define DIGEST_SIZE_MD5  32
#define DIGEST_SIZE_SHA1 40

int mhexdigest(FILE *stream, unsigned char *mdhex, int *mdhex_size, int digest_type);
int mdigest(FILE *stream, unsigned char *md, int *md_size, int digest_type); 

/*
  Returns $TMPDIR or "/tmp" if $TMPDIR isn't set.
  Returned dir always begin with '/'
*/
const char *setup_cachedir(void);
const char *tmpdir(void);

const char *expand_env_vars(char *dest, int size, const char *str);

void die(void);

char *trimslash(char *path);
char *next_token(char **str, char delim, int *toklen);
int is_rwxdir(const char *path);
int is_dir(const char *path);
int mk_dir(const char *path, const char *dn);

const char *abs_path(char *buf, int size, const char *path);

extern int mem_info_verbose;
void print_mem_info(const char *prefix);
void mem_info(int level, const char *msg);

void process_cmd_output(struct p_open_st *st, const char *prefix);
int lockfile(const char *lockfile);
pid_t readlockfile(const char *lockfile);

int mklock(const char *dir);

void display_pkg_list(int verbose_l, const char *prefix,
                      tn_array *pkgs, unsigned flags);



#endif /* POLDEK_MISC_H */
