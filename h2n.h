#ifndef POLDEK_HTON_H
#define POLDEK_HTON_H

#ifdef HAVE_CONFIG_H            /* for inline */
# include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>

#include <trurl/nbuf.h>

#define hton16(v)  htons(v)
#define hton32(v)  htonl(v)

#define ntoh16(v)  ntohs(v)
#define ntoh32(v)  ntohl(v)


#define n_buf_add_int8(nbuf, v)  \
        n_buf_add(nbuf, &v, sizeof(v));


#define n_buf_add_int16(nbuf, v)           \
    do {                                   \
         uint16_t nv = hton16(v);          \
         n_buf_add(nbuf, &nv, sizeof(nv)); \
    } while(0);


#define n_buf_add_int32(nbuf, v)           \
    do {                                   \
         uint32_t nv = hton32(v);          \
         n_buf_add(nbuf, &nv, sizeof(nv)); \
    } while(0);


static inline
int n_buf_it_get_int8(tn_buf_it *nbufi, uint8_t *vp) 
{
    char *p;
    
    p = n_buf_it_get(nbufi, sizeof(*vp));
    if (p == NULL)
        return 0;
    
    *vp = *(uint8_t*)p;
    return 1;
}

static inline
int n_buf_it_get_int16(tn_buf_it *nbufi, uint16_t *vp) 
{
    char *p;
    
    p = n_buf_it_get(nbufi, sizeof(*vp));
    if (p == NULL)
        return 0;
    
    memcpy(vp, p, sizeof(*vp));
    *vp = ntoh16(*vp);
    return 1;
}

static inline
int n_buf_it_get_int32(tn_buf_it *nbufi, uint32_t *vp) 
{
    char *p;
    
    p = n_buf_it_get(nbufi, sizeof(*vp));
    if (p == NULL)
        return 0;

    memcpy(vp, p, sizeof(*vp));
    *vp = ntoh32(*vp);
    return 1;
}


#endif /* POLDEK_HTON_H */
