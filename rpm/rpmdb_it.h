/* $Id$ */
#ifndef POLDEK_RPMDB_ITERATOR_H
#define POLDEK_RPMDB_ITERATOR_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <rpm/rpmlib.h>

struct dbrec {
    unsigned  recno;
    Header    h;
};

void dbrec_clean(struct dbrec *dbrec); /* free members */
char *dbrec_snprintf(char *buf, size_t size, const struct dbrec *dbrec);
char *dbrec_snprintf_s(const struct dbrec *dbrec);

#define RPMITER_RECNO     0
#define RPMITER_NAME      1
#define RPMITER_CAP       2
#define RPMITER_REQ       3
#define RPMITER_CNFL      4
#define RPMITER_OBSL      5
#define RPMITER_FILE      6

/* remeber! don't touch any member */
struct rpmdb_it {
    
#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator   mi;
#else     
    dbiIndexSet          matches;
    int                  i;
#endif
    
    struct dbrec         dbrec;
    rpmdb                db; 
};


int rpmdb_it_init(rpmdb db, struct rpmdb_it *it, int tag,
                  const char *arg);

void rpmdb_it_destroy(struct rpmdb_it *it);

const struct dbrec *rpmdb_it_get(struct rpmdb_it *it);

int rpmdb_it_get_count(struct rpmdb_it *it);

#endif
