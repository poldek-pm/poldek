
#include "pm/mod.h"
#include "pm_pset.h"

struct pm_module pm_module_pset = {
    0, "pset",
    (void *(*)(void *))pm_pset_init, pm_pset_destroy,
    
    NULL,
    
    NULL, 
    NULL,
    NULL,
    
    (void *(*)(void *, const char *, const char *, mode_t))pm_pset_opendb,
    (void (*)(void *))pm_pset_closedb,

    pm_pset_db_it_init,
    
    NULL,
    NULL,

    pm_pset_packages_install, 
    pm_pset_packages_uninstall,
    NULL,
    
    pm_psethdr_nevr,
    pm_pset_ldhdr,                       /* ldhdr */
    pm_pset_ldhdr_capreqs,               /* ldhdr_capreqs */
    NULL,            /* ldpkg */
    NULL,
};

    
        
    

        
    
