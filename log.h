/* $Id$ */
#ifndef POLDEK_LOG_H
#define POLDEK_LOG_H

#include <stdio.h>
#include <stdarg.h>

#define	LOGERR	        (1 << 1)	/* error conditions */
#define	LOGWARN	        (1 << 2)	/* warning conditions */
#define	LOGINFO	        (1 << 4)	/* informational */
#define	LOGDEBUG	(1 << 5)	/* debug-level messages */

extern int verbose;

int log_openlog(const char *pathname, unsigned mask, char *prefix);
int log_sopenlog(FILE *stream, unsigned mask, char *prefix);
void log_closelog(void);

FILE *log_stream(void);

void log_msg(const char *fmt, ...);
void log_msg_i(int indent, const char *fmt, ...);

void msg(int verbose_level, const char *fmt, ...);
#define msg(verbose_level, fmt, args...)   \
  do {                                     \
    if ((verbose_level) <= verbose)        \
      log_msg(fmt, ## args);               \
  } while(0)

#define msg_i(verbose_level, indent, fmt, args...)   \
  do {                                               \
    if ((verbose_level) <= verbose)                  \
      log_msg_i(indent, fmt, ## args);               \
  } while(0)

void vlog(int pri, int indent, const char *fmt, va_list args);
void log(int pri, const char *fmt, ...);

#define log_debug(fmt, args...) \
  log(LOG_DEBUG, "%s: " fmt, __FUNCTION__ , ## args)

void log_set_vprintf(int (*vprintffn)(const char *format, va_list args));


#ifndef DBG
# define DBGMSG_F(fmt, args...)  ((void) 0)
# define DBGMSG(fmt, args...)    ((void) 0)
#else
# define DBGMSG_F(fmt, args...) printf("%s: " fmt, __FUNCTION__ , ## args)
# define DBGMSG(fmt, args...) printf(fmt, ## args)
#endif                                  

#endif
