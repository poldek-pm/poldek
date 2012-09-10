/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "pm/mod.h"
#include "pm_rpm.h"

struct pm_module pm_module_rpm = {
    0, "rpm",
    (void *(*)(void))pm_rpm_init, pm_rpm_destroy,
    pm_rpm_configure,
    pm_rpm_conf_get,

    pm_rpm_satisfies,
    pm_rpm_dbpath, 
    pm_rpm_dbmtime,
    pm_rpm_dbdepdirs,
    
(void *(*)(void *, void *, const char *, const char *, mode_t, tn_hash *))pm_rpm_opendb,
    (void (*)(void *))pm_rpm_closedb,
    NULL,                       /* txbegin */
    NULL,                       /* txcommit */
    NULL,                       /* dbfree */

    pm_rpm_db_it_init,
    
    pm_rpm_install_package,
    pm_rpm_vercmp,

    pm_rpm_packages_install, 
    pm_rpm_packages_uninstall,
    pm_rpm_verify_signature,
    
    pm_rpmhdr_nevr,
    pm_rpmhdr_link,
    pm_rpmhdr_free, 
    
    (struct pkg *(*)(tn_alloc *, void *, const char *, unsigned, unsigned))pm_rpm_ldhdr,
    (tn_array *(*)(tn_array *, void *, int))pm_rpm_ldhdr_capreqs,
    pm_rpm_ldpkg,
    pm_rpm_db_to_pkgdir,
    pm_rpm_machine_score,
};

    
        
    

        
    
