/* $Id$ */
#ifndef POLDEK_CAPREQ_H
#define POLDEK_CAPREQ_H

#ifdef HAVE_CONFIG_H            /* for inline */
# include "config.h"
#endif

#include <stdint.h>

#include <trurl/narray.h>
#include <trurl/nbuf.h>

#define REL_EQ	    (1 << 0)
#define REL_GT	    (1 << 1)
#define REL_LT	    (1 << 2)

#define REL_ALL     (REL_EQ | REL_GT | REL_LT)

#if 0
/* types */
#define CAPREQ_PROV     (1 << 0)
#define CAPREQ_REQ      (1 << 1)
#endif
#define CAPREQ_CNFL     (1 << 2)

/* sub types */
#define CAPREQ_PREREQ      (1 << 3)         /* '*' prefix */
#define CAPREQ_PREREQ_UN   (1 << 4)         /* '^' prefix */

#define CAPREQ_OBCNFL      CAPREQ_PREREQ    /* alias, for obsolences */

#define CAPREQ_RPMLIB            (1 << 5)   /* rpmlib(...) */

#define CAPREQ_RPMLIB_SATISFIED  (1 << 6)   /* is rpmlib provides rpmlib(...)? */
#define CAPREQ_BASTARD   (1 << 7)   /* capreq added by poldek during mkidx,
                                        '!' prefix */

/* 'runtime' i.e. not storable flags  */
#define CAPREQ_RT_FLAGS    (CAPREQ_RPMLIB_SATISFIED | CAPREQ_BASTARD)

struct capreq {
    uint8_t  cr_flags;
    uint8_t  cr_relflags;
    char     *name;
/*  uint8_t cr_name_ofs = 1, always */
    uint8_t  cr_ep_ofs;
    uint8_t  cr_ver_ofs;         /* 0 if capreq hasn't version */
    uint8_t  cr_rel_ofs;         /* 0 if capreq hasn't release */
    char    _buf[0];             /* alias cr_name, first byte is always '\0' */
};

/* CAUTION: side effects! */
//#define capreq_name(cr)     &(cr)->_buf[1]
#define capreq_name(cr)     (const char*)(cr)->name
    
extern inline int32_t capreq_epoch_(const struct capreq *cr);

#define capreq_epoch(cr) \
    ((cr)->cr_ep_ofs ? capreq_epoch_(cr) : 0)

#define capreq_ver(cr)  (&(cr)->_buf[(cr)->cr_ver_ofs])
#define capreq_rel(cr)  (&(cr)->_buf[(cr)->cr_rel_ofs])

#define capreq_has_epoch(cr)    (cr)->cr_ep_ofs   
#define capreq_has_ver(cr)      (cr)->cr_ver_ofs
#define capreq_has_rel(cr)      (cr)->cr_rel_ofs
#define capreq_versioned(cr)    ((cr)->cr_relflags & (REL_ALL))


#define capreq_is_cnfl(cr)      ((cr)->cr_flags & CAPREQ_CNFL)
#define capreq_is_prereq(cr)    ((cr)->cr_flags & CAPREQ_PREREQ)
#define capreq_is_prereq_un(cr) ((cr)->cr_flags & CAPREQ_PREREQ_UN)
#define capreq_is_obsl(cr)        capreq_is_prereq((cr))
#define capreq_is_file(cr)      ((cr)->_buf[1] == '/')
#define capreq_isnot_file(cr)   ((cr)->_buf[1] != '/')

#define capreq_is_bastard(cr)   ((cr)->cr_flags & CAPREQ_BASTARD)

#define capreq_is_rpmlib(cr)     ((cr)->cr_flags & CAPREQ_RPMLIB)
#define capreq_set_satisfied(cr)  ((cr)->cr_flags |= CAPREQ_RPMLIB_SATISFIED)
#define capreq_clr_satisfied(cr)  ((cr)->cr_flags &= (~CAPREQ_RPMLIB_SATISFIED))
#define capreq_is_satisfied(cr)  ((cr)->cr_flags & CAPREQ_RPMLIB_SATISFIED)

#define capreq_revrel(cr) ((cr)->cr_relflags = (cr)->cr_relflags ? \
                          (((uint8_t)~cnfl->cr_relflags) & REL_ALL) : (cr)->cr_relflags)

//#define capreq_is_resolved(cr)   ((cr)->cr_flags & CAPREQ_RESOLVED)
//#define capreq_mark_resolved(cr) ((cr)->cr_flags |= CAPREQ_RESOLVED)

struct capreq *capreq_new_evr(const char *name, char *evr, int32_t relflags,
                              int32_t flags);

struct capreq *capreq_new(tn_alloc *na, const char *name, int32_t epoch,
                          const char *version, const char *release,
                          int32_t relflags, int32_t flags);

#define capreq_new_name_a(name, crptr)	     \
    {                                        \
        struct capreq *__cr;                   \
        int len = strlen((name));            \
        __cr = alloca(sizeof(*__cr) + len + 2);  \
        __cr->cr_flags = __cr->cr_relflags = 0;  \
        __cr->cr_ep_ofs = __cr->cr_ver_ofs = __cr->cr_rel_ofs = 0; \
        __cr->_buf[0] = '\0';                  \
        memcpy(&__cr->_buf[1], (name), len + 1);  \
        crptr = __cr;                            \
    }

void capreq_free_na(tn_alloc *na, struct capreq *cr);
void capreq_free(struct capreq *cr);

struct capreq *capreq_clone(tn_alloc *na, const struct capreq *cr);

uint8_t capreq_sizeof(const struct capreq *cr);

#if 0
void capreq_store(struct capreq *cr, tn_buf *nbuf);
struct capreq *capreq_restore(tn_buf_it *nbufi);
#endif

int capreq_strcmp_evr(struct capreq *pr1, struct capreq *pr2);
int capreq_strcmp_name_evr(struct capreq *pr1, struct capreq *pr2);

int capreq_cmp_name(struct capreq *cr1, struct capreq *cr2);
int capreq_cmp2name(struct capreq *pr1, const char *name);
int capreq_cmp_name_evr(struct capreq *cr1, struct capreq *cr2);

tn_array *capreq_arr_new(int size);
int capreq_arr_find(tn_array *capreqs, const char *name);

int capreq_arr_store_n(tn_array *arr);
tn_buf *capreq_arr_store(tn_array *arr, tn_buf *nbuf, int n);
//int capreq_arr_store_st(tn_array *arr, const char *prefix, tn_stream *st);

tn_array *capreq_arr_restore(tn_alloc *na, tn_buf *nbuf);
tn_array *capreq_arr_restore_st(tn_alloc *na, tn_stream *st);

tn_array *capreq_pkg(tn_array *arr, int32_t epoch, 
                     const char *name, int name_len, 
                     const char *version, int version_len, 
                     const char *release, int release_len);


int capreq_snprintf(char *str, size_t size, const struct capreq *cr);
char *capreq_snprintf_s(const struct capreq *cr);
char *capreq_snprintf_s0(const struct capreq *cr);


void set_capreq_allocfn(void *(*cr_allocfn)(size_t), void (*cr_freefn)(void*),
                        void **prev_alloc, void **prev_free);

#endif /* POLDEK_CAPREQ_H */
