%module poldekmod

%{
#include "local_stdint.h"
#include "poldek.h"
#include "trurl/narray.h"
#include "capreq.h"    
#include "pkg.h"
#include "pkgu.h"
#include "poldek.h"
#include "poldek_ts.h"
#include "pkgdir/source.h"
#include "pkgdir/pkgdir.h"
#include "cli/poclidek.h"
#include "cli/dent.h"
%}
%include exception.i
%include "local_stdint.h"
%include "trurl/narray.h"
%include "capreq.h"
%include "pkg.h"
%include "pkgu.h"
%include "poldek.h"
%include "poldek_ts.h"
%include "pkgdir/source.h"
%include "pkgdir/pkgdir.h"
%include "cli/poclidek.h"
%include "cli/dent.h"


struct poldek_ctx {};
struct poldek_ts {};
struct pkguinf {};
struct pkgflist_it {};

struct poclidek_ctx {};
struct poclidek_rcmd {};

%extend capreq {
    capreq(void *ptr) { return ptr; } /* conv constructor */
    capreq(const char *name, int32_t epoch,
           const char *version, const char *release,
           int32_t relflags, int32_t flags)
        {
            return capreq_new(NULL, name, epoch, version, release,
                              relflags, flags);
        }

    capreq(const char *name, const char *evr, 
           int32_t relflags, int32_t flags)
        {
            int len = strlen(evr);
            char *evr_ = alloca(len + 1);
            memcpy(evr_, evr, len + 1);
            return capreq_new_evr(name, evr_, relflags, flags);
        }


    const char *get_name() { return capreq_name(self); }
    int32_t get_epoch() { return capreq_epoch(self); }
    const char *get_ver() { return capreq_ver(self); }
    const char *get_rel() { return capreq_rel(self); }
    int is_versioned() { return capreq_versioned(self); }
    
    ~capreq() { capreq_free(self); }
}

%extend pkg_dent {
    pkg_dent(void *dent) { return dent; };
    ~pkg_dent() { pkg_dent_free(self); }
    
    #struct pkg *pkg() { if (self->flags & PKG_DENT_DIR) return NULL; return self->ent->pkg; }
        #    tn_array *ENTS() { if (self->flags & PKG_DENT_DIR) return self->ent->ents; return NULL  }
}            


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

%exception nth { 
    $function 
    if (!result) {
        PyErr_SetString(PyExc_IndexError, "Index out of bounds"); 
        return NULL;
    }
}
     

%extend tn_array {
    tn_array(int size) { return n_array_new_ex(size, NULL, NULL, NULL); };
    tn_array(void *arr) { return n_ref(arr); };
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


/**
   opaque redeclaration of fl iterator 

**/
struct pkgfl_it {};
struct tn_tuple {};

%{

struct pkgfl_it *pkgfl_it_new(tn_tuple *fl);
const char *pkgfl_it_get(struct pkgfl_it *it, struct flfile **flfile);

struct pkgfl_it {};
struct tn_tuple {};

%}

struct pkgfl_it *pkgfl_it_new(tn_tuple *fl);
const char *pkgfl_it_get(struct pkgfl_it *it, struct flfile **flfile);

%extend pkgfl_it {
    pkgfl_it(struct pkgfl_it *ptr) { return ptr; } /* conv constructor */
    pkgfl_it(tn_tuple *fl) { return pkgfl_it_new(fl); }
    const char *next() { return pkgfl_it_get(self, NULL); }
    PyObject *get_tuple() { 
        int32_t size;
        uint16_t mode;
        const char *bn, *path;
        PyObject *tuple = NULL;

        if ((path = pkgfl_it_get_rawargs(self, &size, &mode, &bn)) == NULL) {
             Py_INCREF(Py_None); 
             return Py_None;
        }
        
        tuple = PyTuple_New(4);
        PyTuple_SET_ITEM(tuple, 0, Py_BuildValue("s", path));
        PyTuple_SET_ITEM(tuple, 1, PyInt_FromLong(size));
        PyTuple_SET_ITEM(tuple, 2, PyInt_FromLong(mode));
        PyTuple_SET_ITEM(tuple, 3, Py_BuildValue("s", bn));
        return tuple;
    }
    ~pkgfl_it(self) { free(self); }
}


%extend pkgflist_it {
    pkgflist_it(struct pkgflist_it *ptr) { return ptr; } /* conv constructor */
    PyObject *get_tuple() { 
        int32_t size;
        uint16_t mode;
        const char *bn, *path;
        PyObject *tuple = NULL;

        if ((path = pkgflist_it_get_rawargs(self, &size, &mode, &bn)) == NULL) {
             Py_INCREF(Py_None); 
             return Py_None;
        }
        
        tuple = PyTuple_New(3);
        PyTuple_SET_ITEM(tuple, 0, Py_BuildValue("s", path));
        PyTuple_SET_ITEM(tuple, 1, PyInt_FromLong(size));
        PyTuple_SET_ITEM(tuple, 2, PyInt_FromLong(mode));
        return tuple;
    }
    ~pkgflist_it(self) { pkgflist_it_free(self); }
}


%extend pkg {
    pkg(void *ptr) { return ptr; } /* conv constructor */
    tn_array *_get_provides() { return self->caps; }
    tn_array *_get_requires() { return self->reqs; }
    struct pkgfl_it *filelist_it() { return pkgfl_it_new(self->fl); }
    ~pkg() { pkg_free(self); }
}

%extend pkguinf {
    pkguinf(void *ptr) { return ptr; } /* conv constructor */
    const char *get(char tag) { return pkguinf_getstr(self, tag); }
    ~pkguinf() { pkguinf_free(self); }
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
    int load_config(const char *path = NULL) { poldek_load_config(self, path, 0, 0); }
    int configure(int param, void *val) {
        if (param == POLDEK_CONF_SOURCE)
            val = source_link(val);
        poldek_configure(self, param, val);
    }
    int configure(int param, unsigned val) { poldek_configure(self, param, val); }
    int configure(int param, char *val) { poldek_configure(self, param, val); }
    struct poldek_ts *ts_new(unsigned flags) { return poldek_ts_new(self, flags); }
}

%immutable poldek_ts;

    
            
