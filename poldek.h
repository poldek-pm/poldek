/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* $Id$ */
#ifndef  POLDEK_LIB_H
#define  POLDEK_LIB_H

#include "pkg.h"
#include "poldek_ts.h"

#include <trurl/narray.h>
#include <trurl/nhash.h>

#ifndef EXPORT
# define EXPORT extern
#endif

/* constans  */
EXPORT const char poldek_BUG_MAILADDR[];
EXPORT const char poldek_VERSION_BANNER[];
EXPORT const char poldek_BANNER[];

EXPORT int poldeklib_init(void);
EXPORT void poldeklib_destroy(void);

struct poldek_ctx;
EXPORT struct poldek_ctx *poldek_new(unsigned flags);
EXPORT void poldek_free(struct poldek_ctx *ctx);
EXPORT struct poldek_ctx *poldek_link(struct poldek_ctx *ctx);

#define POLDEK_CONF_OPT             0
#define POLDEK_CONF_CACHEDIR        3 
#define POLDEK_CONF_FETCHDIR        4
#define POLDEK_CONF_ROOTDIR         5 
#define POLDEK_CONF_DUMPFILE        6
#define POLDEK_CONF_PRIFILE         7
#define POLDEK_CONF_SOURCE          8
#define POLDEK_CONF_RPMMACROS       9
#define POLDEK_CONF_RPMOPTS         10
#define POLDEK_CONF_HOLD            11
#define POLDEK_CONF_IGNORE          12
#define POLDEK_CONF_PM              13
#define POLDEK_CONF_DESTINATION     14
#define POLDEK_CONF_DEPGRAPH        15
#define POLDEK_CONF_LOGFILE         20
#define POLDEK_CONF_LOGTTY          21

#define POLDEK_CONF_GOODBYE_CB         22
#define POLDEK_CONF_CONFIRM_CB         23
#define POLDEK_CONF_TSCONFIRM_CB       24
#define POLDEK_CONF_CHOOSEEQUIV_CB     25
#define POLDEK_CONF_CHOOSESUGGESTS_CB  26
#define POLDEK_CONF_VFILEPROGRESS      27

EXPORT int poldek_configure(struct poldek_ctx *ctx, int param, ...);

#define POLDEK_LOADCONF_NOCONF (1 << 0) /* do not load configuration from file */
#define POLDEK_LOADCONF_UPCONF (1 << 1) /* do update of remote config files    */
EXPORT int poldek_load_config(struct poldek_ctx *ctx, const char *path,
                       tn_array *addon_cnflines, unsigned flags);

EXPORT int poldek_setup_cachedir(struct poldek_ctx *ctx);
EXPORT int poldek_setup(struct poldek_ctx *ctx);

EXPORT int poldek_load_sources(struct poldek_ctx *ctx);

EXPORT int poldek_is_interactive_on(const struct poldek_ctx *ctx);


EXPORT tn_array *poldek_get_sources(struct poldek_ctx *ctx);
EXPORT tn_array *poldek_get_pkgdirs(struct poldek_ctx *ctx);

struct pm_ctx;
EXPORT struct pm_ctx *poldek_get_pmctx(struct poldek_ctx *ctx);

EXPORT tn_hash *poldek_get_config(struct poldek_ctx *ctx);

enum poldek_search_tag {
    POLDEK_ST_RECNO = 1,
    POLDEK_ST_NAME  = 2,
    POLDEK_ST_CAP   = 3,        /* what provides cap */
    POLDEK_ST_REQ   = 4,        /* what requires */
    POLDEK_ST_CNFL  = 5,        
    POLDEK_ST_OBSL  = 6,
    POLDEK_ST_FILE  = 7,
    POLDEK_ST_PROVIDES = 8,     /* what provides cap or file */
};

EXPORT tn_array *poldek_search_avail_packages(struct poldek_ctx *ctx,
                                       enum poldek_search_tag tag,
                                       const char *value);

EXPORT tn_array *poldek_get_avail_packages(struct poldek_ctx *ctx);


EXPORT struct pkgdir *poldek_load_destination_pkgdir(struct poldek_ctx *ctx, 
                                              unsigned ldflags);

EXPORT int poldek_split(const struct poldek_ctx *ctx, unsigned size_mb,
                 unsigned first_free_space_mb, const char *outprefix);

#endif 
