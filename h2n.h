#ifndef POLDEK_HTON_H
#define POLDEK_HTON_H

#include <netinet/in.h>

#define hton16(v)  htons(v)
#define hton32(v)  htonl(v)

#define ntoh16(v)  ntohs(v)
#define ntoh32(v)  ntohl(v)

#endif /* POLDEK_HTON_H */
