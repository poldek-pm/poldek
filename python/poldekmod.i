%module poldekmod

%{
#include "poldek.h"
#include "trurl/narray.h"
#include "trurl/nhash.h"
#include "capreq.h"    
#include "pkg.h"
#include "pkgu.h"
#include "poldek.h"
#include "poldek_ts.h"
#include "pkgdir/source.h"
#include "pkgdir/pkgdir.h"
#include "cli/poclidek.h"
#include "log.h"
#include "vfile/vfile.h" /* for vf_progress */

static void PythonDoLog(void *data, int pri, const char *message);
static int PythonConfirm(void *data, const struct poldek_ts *ts, 
                         int hint, const char *message);
static int PythonTsConfirm(void *data, const struct poldek_ts *ts);

static int PythonChooseEquiv(void *data, const struct poldek_ts *ts,
                             const struct pkg *pkg, const char *cap,
                             tn_array *pkgs, int hint);

static int PythonChooseSuggests(void *data, const struct poldek_ts *ts, 
                                const struct pkg *pkg, tn_array *caps, 
                                tn_array *choices, int hint);

static struct vf_progress vfPyProgress;
%}

%include exception.i
%include "trurl/narray.h"
%include "capreq.h"
%include "pkg.h"
%include "pkgu.h"
%include "poldek.h"
%include "poldek_ts.h"
%include "pkgdir/source.h"
%include "pkgdir/pkgdir.h"
%include "cli/poclidek.h"

struct poldek_ctx {};
struct poldek_ts {};
struct pkguinf {};
struct pkgflist_it {};

struct poclidek_ctx {};
struct poclidek_rcmd {};


%{
static void *py_progress_new(void *data, const char *label)
{
   PyObject *obj, *method, *pylabel, *r;
     
   obj = (PyObject *) data;                        
   method = Py_BuildValue("s", "initialize");
   pylabel = Py_BuildValue("s", label);
     
   r = PyObject_CallMethodObjArgs(obj, method, pylabel, NULL);
   if (r)
      Py_DECREF(r);

   Py_DECREF(method);
   Py_DECREF(pylabel);     
   return obj;
}

static void py_progress_reset(void *bar) {
   PyObject *obj, *method, *r;
     
   obj = (PyObject *) bar;                        
   method = Py_BuildValue("s", "reset");

   r = PyObject_CallMethodObjArgs(obj, method, NULL);
   if (r)
      Py_DECREF(r);

   Py_DECREF(method);
}

static void py_progress(void *bar, long total, long amount) {
   PyObject *obj, *method, *pytotal, *pyamount, *r;
     
   obj = (PyObject *) bar;                        
   method = Py_BuildValue("s", "progress");
   pytotal = Py_BuildValue("i", total);
   pyamount = Py_BuildValue("i", amount);

   r = PyObject_CallMethodObjArgs(obj, method, pytotal, pyamount, NULL);
   if (r)
      Py_DECREF(r);

   Py_DECREF(method);
   Py_DECREF(pytotal);
   Py_DECREF(pyamount);
}

static struct vf_progress vfPyProgress = {
    NULL, py_progress_new, py_progress, py_progress_reset, NULL
};

static void PythonDoLog(void *data, int pri, const char *message)
{
   PyObject *obj, *method, *pypri, *pymessage, *r;
   const char *spri = "info";

   if (pri & LOGERR)
       spri = "error";

   else if (pri & LOGWARN)
       spri = "warning";

   else if (pri & LOGNOTICE)
       spri = "notice";

   else if (pri & LOGOPT_CONT)
       spri = "cont";
     
   obj = (PyObject *) data;                        
   method = Py_BuildValue("s", "log");
   
   pypri = Py_BuildValue("s", spri);       
   pymessage = Py_BuildValue("s", message);

   r = PyObject_CallMethodObjArgs(obj, method, pypri, pymessage, NULL);
   if (r)
      Py_DECREF(r);
   Py_DECREF(method);
   Py_DECREF(pypri);
   Py_DECREF(pymessage);
}

static int PythonConfirm(void *data, const struct poldek_ts *ts, 
                         int hint, const char *message)
{
   PyObject *obj, *method, *pyts, *pyhint, *pymessage, *r; 
   int answer;
     
   obj = (PyObject *) data;
   method = Py_BuildValue("s", "confirm");

   pyts = SWIG_NewPointerObj(SWIG_as_voidptr(ts), SWIGTYPE_p_poldek_ts, 0 | 0);
   Py_INCREF(pyts);            // XXX - SWIG_NewPointerObj do incref?

   pyhint = Py_BuildValue("i", hint);     
   pymessage = Py_BuildValue("s", message);

   r = PyObject_CallMethodObjArgs(obj, method, pyts, pyhint, pymessage, NULL);

   Py_DECREF(method);
   Py_DECREF(pyts);
   Py_DECREF(pyhint);
   Py_DECREF(pymessage); 
     
   if (r == NULL)
      return hint;

   answer = PyObject_IsTrue(r);
   Py_DECREF(r);
   return answer;     
}

static int PythonTsConfirm(void *data, const struct poldek_ts *ts)
{
   PyObject *obj, *method, *pyts, *r; 
   int answer;
     
   obj = (PyObject *) data;
   method = Py_BuildValue("s", "confirm_transaction");
   pyts = SWIG_NewPointerObj(SWIG_as_voidptr(ts), SWIGTYPE_p_poldek_ts, 0 | 0);
   Py_INCREF(pyts); // XXX - SWIG_NewPointerObj do incref?     

   r = PyObject_CallMethodObjArgs(obj, method, pyts, NULL);
   Py_DECREF(pyts);
   Py_DECREF(method);
     
   if (r == NULL)
      return 0;

   answer = PyObject_IsTrue(r);
   Py_DECREF(r);
   return answer;     
}

static int PythonChooseEquiv(void *data, const struct poldek_ts *ts, 
                             const struct pkg *pkg, const char *cap, 
                             tn_array *pkgs, int hint)
{
   PyObject *obj, *method, *pyts, *pypkg, *pycap, *pypkgs, *pyhint, *r; 
   tn_array *packages = n_ref(pkgs);  // XXX
   int answer;
     
   obj = (PyObject *) data;
   method = Py_BuildValue("s", "raw__choose_equiv"); // XXX - see poldek.py

   pyts = SWIG_NewPointerObj(SWIG_as_voidptr(ts), SWIGTYPE_p_poldek_ts, 0 | 0);
   Py_INCREF(pyts);            // XXX - SWIG_NewPointerObj do incref?

   pypkg = SWIG_NewPointerObj(SWIG_as_voidptr(pkg), SWIGTYPE_p_pkg, 0 | 0);
   Py_INCREF(pypkg);            // XXX - SWIG_NewPointerObj do incref?

   pycap = Py_BuildValue("s", cap);     
   pypkgs = SWIG_NewPointerObj(SWIG_as_voidptr(pkgs), SWIGTYPE_p_trurl_array_private, 0 | 0);
   Py_INCREF(pypkgs);

   pyhint = Py_BuildValue("i", hint);     

   r = PyObject_CallMethodObjArgs(obj, method, pyts, pypkg, pycap, pypkgs, pyhint, NULL);

   Py_DECREF(method);
   Py_DECREF(pyts);
   Py_DECREF(pypkg);
   Py_DECREF(pycap);
   Py_DECREF(pypkgs);
   Py_DECREF(pyhint);
     
   if (r == NULL)
      return hint;

   answer = (int)PyLong_AsLong(r);
   Py_DECREF(r);
   return answer;     
}

static int PythonChooseSuggests(void *data, const struct poldek_ts *ts, 
                                const struct pkg *pkg, tn_array *caps, 
                                tn_array *choices, int hint)
{
   PyObject *obj, *method, *pyts, *pypkg, *pycaps, *pychoices, *pyhint, *r; 
   int answer;
     
   obj = (PyObject *) data;
   method = Py_BuildValue("s", "raw__choose_suggests"); // XXX - see poldek.py

   pyts = SWIG_NewPointerObj(SWIG_as_voidptr(ts), SWIGTYPE_p_poldek_ts, 0 | 0);
   Py_INCREF(pyts);            // XXX - SWIG_NewPointerObj do incref?

   pypkg = SWIG_NewPointerObj(SWIG_as_voidptr(pkg), SWIGTYPE_p_pkg, 0 | 0);
   Py_INCREF(pypkg);            // XXX - SWIG_NewPointerObj do incref?

   pycaps = SWIG_NewPointerObj(SWIG_as_voidptr(caps), SWIGTYPE_p_trurl_array_private, 0 | 0);
   Py_INCREF(pycaps);

   pychoices = SWIG_NewPointerObj(SWIG_as_voidptr(choices), SWIGTYPE_p_trurl_array_private, 0 | 0);
   Py_INCREF(pychoices);


   pyhint = Py_BuildValue("i", hint);     

   r = PyObject_CallMethodObjArgs(obj, method, pyts, pypkg, pycaps, pychoices, pyhint, NULL);

   Py_DECREF(method);
   Py_DECREF(pyts);
   Py_DECREF(pypkg);
   Py_DECREF(pycaps);
   Py_DECREF(pychoices);
   Py_DECREF(pyhint);
     
   if (r == NULL)
      return hint;

   answer = (int)PyLong_AsLong(r);
   Py_DECREF(r);
   return answer;     
}

%}

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
            return capreq_new_evr(NULL, name, evr_, relflags, flags);
        }


    const char *get_name() { return capreq_name(self); }
    int32_t get_epoch() { return capreq_epoch(self); }
    const char *get_ver() { return capreq_ver(self); }
    const char *get_rel() { return capreq_rel(self); }
    int is_versioned() { return capreq_versioned(self); }
    int _is_cnfl() { return capreq_is_cnfl(self); }
    int _is_prereq() { return capreq_is_prereq(self); }
    int _is_prereq_un() { return capreq_is_prereq_un(self); }
    int _is_file() { return capreq_is_file(self); }
    int _is_bastard() { return capreq_is_bastard(self); }
    int _is_autodirreq() { return capreq_is_autodirreq(self); }
    int _is_obsl() { return capreq_is_obsl(self); }
    int _is_rpmlib() { return capreq_is_rpmlib(self); }    
    ~capreq() { capreq_free(self); }
}

/* not so useful
%extend pkg_dent {
    pkg_dent(void *dent) { return dent; };
    ~pkg_dent() { pkg_dent_free(self); }
    
    #struct pkg *pkg() { if (self->flags & PKG_DENT_DIR) return NULL; return self->ent->pkg; }
        #    tn_array *ENTS() { if (self->flags & PKG_DENT_DIR) return self->ent->ents; return NULL  }
}            
*/

%extend poclidek_rcmd {
    poclidek_rcmd(struct poclidek_ctx *cctx, struct poldek_ts *ts) {
        return poclidek_rcmd_new(cctx, ts);
    }

    poclidek_rcmd(struct poclidek_ctx *cctx) {
        return poclidek_rcmd_new(cctx, NULL);
    }

    int execute(const char *cmdline) {
        return poclidek_rcmd_execline(self, cmdline);
    }

    char *to_s() {
        const char *s = poclidek_rcmd_get_output(self);
        if (s) 
           return n_strdup(s);
        return NULL;
    }    

    ~poclidek_rcmd() { poclidek_rcmd_free(self); }
};

%extend poclidek_ctx {
    poclidek_ctx(struct poldek_ctx *ctx) {
        return poclidek_new(ctx);
    }
    
    char *pwd() { 
        char path[1024]; 
        if (poclidek_pwd(self, path, sizeof(path)))
            return n_strdup(path);
        return NULL;
    }

    int chdir(const char *dir) { 
        return poclidek_chdir(self, dir);
    }

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
    ~tn_array() { n_array_free(self); }
    int __len__() { return n_array_size(self); }
        
    void *nth(int i) {
        if (i < n_array_size(self))
            return n_array_nth(self, i);
        return NULL;
    }
}

%extend source {
    source(const char *name, const char *type,
           const char *path, const char *pkg_prefix) {
        return source_new(name, type, path, pkg_prefix);
    }
    
    source(const char *name) {
        return source_new(name, NULL, NULL, NULL); }

    source(void *src) { return source_link(src); };        

    char *__str__() { 
        char *id = NULL;
        if (self->flags & PKGSOURCE_NAMED)
           id = self->name;
        else {  
           char path[PATH_MAX];
           vf_url_slim(path, sizeof(path), self->path, 0);
           id = path;
        }
        return n_strdup(id);
    }

    int get_enabled() {
        //printf("get_enabled %d\n", (self->flags & PKGSOURCE_NOAUTO) == 0);
        return (self->flags & PKGSOURCE_NOAUTO) == 0;
    }       

    void set_enabled(int v) {
        if (v)
           self->flags &= ~PKGSOURCE_NOAUTO;
        else
           self->flags |= PKGSOURCE_NOAUTO;

        //printf("%s enabled=%d\n", self->name, (self->flags & PKGSOURCE_NOAUTO) == 0);
    }   

    ~source() { source_free(self); }
}

%extend pkgdir {
    pkgdir(struct source *src, unsigned flags) {
        return pkgdir_srcopen(src, flags);
    }
    pkgdir(void *pkgdir) { return pkgdir; };        
    tn_array *get_packages() { return self->pkgs; }    

    char *__str__() { // TODO: move it to C codebase
        char *id = NULL;
        if (self->flags & PKGDIR_NAMED)
           id = self->name;
        else {  
           char path[PATH_MAX], *p;
           p = self->idxpath;     
           if (p == NULL)
               p = self->path;   
           if (p) {
              vf_url_slim(path, sizeof(path), p, 0);
              p = path;  
           } else {
              p = "anon";
           }
       
           id = p;
        }
        return n_strdup(id);
    }

    ~pkgdir() { pkgdir_free(self); }
}


%extend pkgflist_it {
    pkgflist_it() { return NULL; }
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
    tn_array *_get_conflicts() { return self->cnfls; }
    tn_array *_get_suggests() { return self->sugs; }
    ~pkg() { pkg_free(self); }
}

%extend pkguinf {
    pkguinf(void *ptr) { return ptr; } /* conv constructor */
    ~pkguinf() { pkguinf_free(self); }
}


%extend poldek_ts {
    poldek_ts(struct poldek_ctx *ctx, unsigned flags) {
        return poldek_ts_new(ctx, flags);
    }
    poldek_ts(struct poldek_ts *ts) {  return ts; }

    ~poldek_ts() {  poldek_ts_free(self); }
}


%extend poldek_ctx {
    poldek_ctx() {
         struct poldek_ctx *ctx = poldek_new(0);
         poldek_configure(ctx, POLDEK_CONF_LOGFILE, NULL);
         poldek_configure(ctx, POLDEK_CONF_LOGTTY, NULL);
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
    struct poldek_ts *ts_new() { return poldek_ts_new(self, 0); }
    int set_verbose(int v) { return poldek_set_verbose(v); }    
    void set_callbacks(PyObject *obj) {
        void *v = (void*)obj;
        poldek_log_set_appender("pyldek", v, NULL, 0, PythonDoLog);
        poldek_configure(self, POLDEK_CONF_CONFIRM_CB, (void*)PythonConfirm, v);
        poldek_configure(self, POLDEK_CONF_TSCONFIRM_CB, (void*)PythonTsConfirm, v);
        poldek_configure(self, POLDEK_CONF_CHOOSEEQUIV_CB, (void*)PythonChooseEquiv, v); 
        poldek_configure(self, POLDEK_CONF_CHOOSESUGGESTS_CB, (void*)PythonChooseSuggests, v); 
        Py_INCREF(obj);
    }

    void set_vfile_progress(PyObject *obj) {
        struct vf_progress *progress;
        progress = n_malloc(sizeof(*progress));
        *progress = vfPyProgress;
        progress->data = obj;
        progress->free = free;
        poldek_configure(self, POLDEK_CONF_VFILEPROGRESS, progress);
        Py_INCREF(obj);
    }
}


%immutable poldek_ts;

    
            
