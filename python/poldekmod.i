%module poldekmod

%{
#include "local_stdint.h"
#include "poldek.h"
#include "trurl/narray.h"
#include "pkg.h"
#include "poldek.h"
#include "poldek_ts.h"
#include "pkgdir/source.h"
#include "pkgdir/pkgdir.h"
#include "cli/poclidek.h"
%}
%include exception.i
%include "local_stdint.h"
%include "trurl/narray.h"
%include "pkg.h"
%include "poldek.h"
%include "poldek_ts.h"
%include "pkgdir/source.h"
%include "pkgdir/pkgdir.h"
%include "cli/poclidek.h"

struct poclidek_ctx {};
struct poclidek_rcmd {};

%extend poclidek_rcmd {
    poclidek_rcmd(struct poclidek_ctx *cctx, struct poldek_ts *ts) {
        return poclidek_rcmd_new(cctx, ts);
    }

    ~poclidek_rcmd() { poclidek_rcmd_free(self); }
};

%extend poclidek_ctx {
    poclidek_ctx(struct poldek_ctx *ctx) {
        return poclidek_new(ctx);
    }

    struct poclidek_rcmd *rcmd_new(struct poldek_ts *ts) {
        return poclidek_rcmd_new(self, ts); }

    ~poclidek_ctx() { poclidek_free(self); }
};



    

extern int poldek_VERBOSE;

%exception nth { 
    $function 
    if (!result) {
        PyErr_SetString(PyExc_IndexError, "Index out of bounds"); 
        return NULL;
    }
}
     

%extend tn_array {
    tn_array(int size) { return n_array_new_ex(size, NULL, NULL, NULL); };
    tn_array(void *arr) { return arr; };
    int __len__() { return n_array_size(self); }
    void *nth(int i) {
        if (i < n_array_size(self))
            return n_array_nth(self, i);
        return NULL;
    }

/* this doesn't raise exception, hgw why
    void *__getitem__(int i) {
        return tn_array_nth(self, i);
    }
*/
        
}


%extend source {
    source(const char *name, const char *type,
           const char *path, const char *pkg_prefix) {
        return source_new(name, type, path, pkg_prefix);
    }
    
    source(const char *name) {
        return source_new(name, NULL, NULL, NULL); }
    ~source() { source_free(self); }
}

%extend pkgdir {
    pkgdir(struct source *src, unsigned flags) {
        return pkgdir_srcopen(src, flags);
    }
    ~pkgdir() { pkgdir_free(self); }
}

%extend pkg {
    pkg(void *ptr) { return ptr; } /* conv constructor */
    ~pkg() { pkg_free(self); }
}


%extend poldek_ts {
    poldek_ts(struct poldek_ctx *ctx, unsigned flags) {
        return poldek_ts_new(ctx, flags);
    }
    
    ~poldek_ts() { poldek_ts_free(self); }
}

%extend poldek_ctx {
    poldek_ctx() {
         struct poldek_ctx *ctx = poldek_new(0);
         return ctx;
    }
    
    ~poldek_ctx() { poldek_free(self); }
    int load_config(const char *path = NULL) { poldek_load_config(self, path, 0); }
    int configure(int param, void *val) { poldek_configure(self, param, val); }
    int configure(int param, unsigned val) { poldek_configure(self, param, val); }
    int configure(int param, char *val) { poldek_configure(self, param, val); }
    struct poldek_ts *ts_new(unsigned flags) { return poldek_ts_new(self, flags); }
}

%immutable poldek_ts;

    
            
