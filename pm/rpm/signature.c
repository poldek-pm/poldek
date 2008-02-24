#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_RPMPKGREAD          /* rpm >= 5.0 */
# include "signature5.c"
#else
# include "signature4.c"
#endif
