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

void term_init();

int printf_c(int color, const char *fmt, ...);
int snprintf_c(int color, char *buf, int size, const char *fmt, ...);
int puts_c(int color, const char *s);

#endif
