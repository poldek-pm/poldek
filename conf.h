/* $Id$ */
#ifndef POLDEK_CONF_H
#define POLDEK_CONF_H

#include <trurl/narray.h>

#if 0
struct conf_downlder 
{
    unsigned urltypes;
    char defmt[0];
};
#endif

struct conf_s {
    char *source;
    char *cachedir;
    char *prefix;
    char *rpm_path;
    char *rpm_args;
    
    char *ftp_http_get;
    char *ftp_get;
    char *http_get;
    char *https_get;
    char *rsync_get;
    tn_array *rpmacros;
#if 0
    tn_array *dnldrs;           /* available downloaders */
#endif    
};

struct conf_s *ldconf(const char *path);
struct conf_s *ldconf_deafult(void);
void conf_s_free(struct conf_s *conf);

#endif

