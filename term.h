/* $Id$  */
#ifndef POLDEK_TERM_H
#define POLDEK_TERM_H

#define PRCOLOR_BLACK    0
#define PRCOLOR_RED      1
#define PRCOLOR_GREEN    2
#define PRCOLOR_YELLOW   3
#define PRCOLOR_BLUE     4
#define PRCOLOR_MAGNETA  5
#define PRCOLOR_CYAN     6

#define PRAT_BOLD        (1 << 15)

#define TERM_DEFAULT_WIDTH  80
#define TERM_DEFAULT_HEIGHT 24

#include <stddef.h>           /* for size_t     */

#ifndef __GNUC__                                                            
#  define __attribute__(x) /*nothing*/                                      
#endif   

int term_init(void);
int term_get_width(void);
int term_get_height(void);

int vprintf_c(int color, const char *fmt, va_list args);

int printf_c(int color, const char *fmt, ...)
   __attribute__((format(printf,2,3)));
   
int snprintf_c(int color, char *str, size_t size, const char *fmt, ...)
   __attribute__((format(printf,4,5)));

int vsnprintf_c(int color, char *str, size_t size, const char *fmt,
                va_list args);

int puts_c(int color, const char *s);

int askuser(int fd, const char *validchrs, const char *msg);

#endif
