
#include "pm/mod.h"
#include "pm_rpm.h"

struct pm_module pm_module_rpm = {
    0, "rpm",
    (void *(*)(void))pm_rpm_init, pm_rpm_destroy,
    pm_rpm_configure,
    pm_rpm_rpmlib_caps,
    
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
//    pm_rpmhdr_link,
//    pm_rpmhdr_free, 
    
    (struct pkg *(*)(tn_alloc *, void *, const char *, unsigned, unsigned))pm_rpm_ldhdr,
    (tn_array *(*)(tn_array *, void *, int))pm_rpm_ldhdr_capreqs,
    pm_rpm_ldpkg,
    pm_rpm_db_to_pkgdir,
    pm_rpm_machine_score,
};

    
        
    

        
    
