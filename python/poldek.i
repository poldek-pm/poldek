%module poldek

%{
#include "poldek.h"
#include "trurl/narray.h"
#include "pkg.h"
#include "poldek.h"
#include "pkgdir/source.h"
#define   NULL 0
%}

%include "trurl/narray.h"
%include "pkg.h"
%include "poldek.h"
%include "pkgdir/source.h"

extern int verbose;

%extend tn_array {
    tn_array(int size) { return n_array_new_ex(size, NULL, NULL, NULL); };
    tn_array(void *arr) { return arr; };
    int __len__() { return n_array_size(self); }
    void *__getitem__(int i) { return n_array_nth(self, i); }
}

%extend pkg {
    pkg(void *ptr) { return ptr; } /* conv constructor */
    ~pkg() { pkg_free(self); }
}

%extend poldek_ctx {
    poldek_ctx() {
         struct poldek_ctx *ctx = malloc(sizeof(*ctx));
         poldek_init(ctx, 0);
         return ctx;
    }
    
    ~poldek_ctx() { poldek_destroy(self); }
    int load_config(const char *path = NULL) { poldek_load_config(self, path); }
    int configure(int param, void *val) { poldek_configure(self, param, val); }
    int configure(int param, unsigned val) { poldek_configure(self, param, val); }
    int configure(int param, char *val) { poldek_configure(self, param, val); }
}

    
            
