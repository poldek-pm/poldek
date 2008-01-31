
#include "pm/mod.h"
#include "pm_pset.h"

struct pm_module pm_module_pset = {
    0, "pset",
    pm_pset_init, pm_pset_destroy,
    pm_pset_configure, 
    NULL,
    pm_pset_satisfies,
//    pm_pset_dbpath, 
//    pm_pset_dbmtime,
//    pm_pset_dbdepdirs,

    NULL, 
    NULL,
    NULL,
    
    pm_pset_opendb,
    pm_pset_closedb,
    pm_pset_tx_begin,
    pm_pset_tx_commit,
    pm_pset_freedb,
    
    pm_pset_db_it_init,
    
    NULL,
    NULL,

    pm_pset_packages_install, 
    pm_pset_packages_uninstall,
    NULL,
    
    pm_pset_hdr_nevr,
    pm_pset_hdr_link,
    pm_pset_hdr_free,
    pm_pset_ldhdr,                       /* ldhdr */
    pm_pset_ldhdr_capreqs,               /* ldhdr_capreqs */
    NULL,            /* ldpkg */
    pm_pset_db_to_pkgdir, 
    NULL,
};

    
        
    

        
    
