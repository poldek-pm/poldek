/* $Id$ */
#ifndef POLDEK_CAPREQ_H
#define POLDEK_CAPREQ_H

#include <stdint.h>

#include <rpm/rpmlib.h>
#include <trurl/narray.h>
#include <trurl/nbuf.h>

#define REL_EQ	    (1 << 0)
#define REL_GT	    (1 << 1)
#define REL_LT	    (1 << 2)

/* types */
#define CAPREQ_PROV     (1 << 3)
#define CAPREQ_REQ      (1 << 4)
#define CAPREQ_CNFL     (1 << 5) 

/* sub types */
#define CAPREQ_PREREQ   (1 << 6)
#define CAPREQ_OBCNFL   CAPREQ_PREREQ  /* alias, for obsolences */

/* is added by poldek? */
#define CAPREQ_PLDEKBAST (1 << 7)       

struct capreq {
/*  uint8_t cr_name_ofs = 1, always */
    uint8_t cr_ep_ofs;
    uint8_t cr_ver_ofs;         /* 0 if capreq hasn't version */
    uint8_t cr_rel_ofs;         /* 0 if capreq hasn't release */
    uint8_t cr_flags;
    char    _buf[0];            /* alias cr_name, first byte is always '\0' */
};

/* CAUTION: side effects! */
#define capreq_name(cr)     &(cr)->_buf[1]
#define capreq_epoch(cr) \
    ((cr)->cr_ep_ofs ? *(int32_t*)&(cr)->_buf[(cr)->cr_ep_ofs] : 0)
#define capreq_ver(cr)  (&(cr)->_buf[(cr)->cr_ver_ofs])
#define capreq_rel(cr)  (&(cr)->_buf[(cr)->cr_rel_ofs])

#define capreq_has_epoch(cr)    (cr)->cr_ep_ofs   
#define capreq_has_ver(cr)      (cr)->cr_ver_ofs
#define capreq_has_rel(cr)      (cr)->cr_rel_ofs

#define capreq_is_prereq(cr)    ((cr)->cr_flags & CAPREQ_PREREQ)
#define cnfl_is_obsl(cr)         capreq_is_prereq((cr))
#define capreq_is_file(cr)      ((cr)->_buf[1] == '/')
#define capreq_isnot_file(cr)   ((cr)->_buf[1] != '/')

#define capreq_is_bastard(cr)   ((cr)->cr_flags & CAPREQ_PLDEKBAST)


//#define capreq_is_resolved(cr)   ((cr)->cr_flags & CAPREQ_RESOLVED)
//#define capreq_mark_resolved(cr) ((cr)->cr_flags |= CAPREQ_RESOLVED)

struct capreq *capreq_new_evr(const char *name, char *evr, int32_t flags);
struct capreq *capreq_new(const char *name, int32_t epoch,
                          const char *version, const char *release,
                          int32_t flags);

int16_t capreq_sizeof(const struct capreq *cr);

void capreq_store(struct capreq *cr, tn_buf *nbuf);
struct capreq *capreq_restore(tn_buf_it *nbufi);

int capreq_cmp_name_evr(struct capreq *pr1, struct capreq *pr2);
//#define capreq_eq(pr1, pr2) (capreq_evr_cmp(pr1, pr2) == 0)
int capreq_cmp2name(struct capreq *pr1, const char *name);

tn_array *capreq_arr_new(void);

#define CRTYPE_CAP  1
#define CRTYPE_REQ  2
#define CRTYPE_CNFL 3
#define CRTYPE_OBSL 4

tn_array *capreqs_get(tn_array *arr, const Header h, int prtype);

#define get_pkg_caps(arr, h)   capreqs_get(arr, h, CRTYPE_CAP)
#define get_pkg_reqs(arr, h)   capreqs_get(arr, h, CRTYPE_REQ)
#define get_pkg_cnfls(arr, h)  capreqs_get(arr, h, CRTYPE_CNFL)
#define get_pkg_obsls(arr, h)  capreqs_get(arr, h, CRTYPE_OBSL)


tn_array *capreq_pkg(tn_array *arr, int32_t epoch, 
                     const char *name, int name_len, 
                     const char *version, int version_len, 
                     const char *release, int release_len);


int capreq_fprintf(FILE *stream, const struct capreq *cr);
char *capreq_snprintf(char *str, size_t size, const struct capreq *cr);
char *capreq_snprintf_s(const struct capreq *cr);


void set_capreq_allocfn(void *(*cr_allocfn)(size_t), void (*cr_freefn)(void*),
                        void **prev_alloc, void **prev_free);

#endif /* POLDEK_CAPREQ_H */
