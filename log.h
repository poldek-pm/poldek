/* $Id$ */
#ifndef POLDEK_LOG_H
#define POLDEK_LOG_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>


#define	LOGTTY	        (1 << 0)	/* log only to TTY output */
#define LOGFILE         (1 << 1)	/* log only to non-TTY output */
#define	LOGERR	        (1 << 8)	/* error conditions */
#define	LOGWARN	        (1 << 9)	/* warning conditions */
#define	LOGINFO	        (1 << 10)	/* informational */
#define	LOGNOTICE       (1 << 11)	/* informational */
#define	LOGDEBUG	(1 << 12)	/* debug-level messages */

extern int verbose;

int log_init(const char *pathname, FILE *tty, char *prefix);
void log_closelog(void);

FILE *log_stream(void);

void vlog(int pri, int indent, const char *fmt, va_list args);
void log(int pri, const char *fmt, ...);

#define log_debug(fmt, args...) \
  log(LOG_DEBUG, "%s: " fmt, __FUNCTION__ , ## args)

#define logv(verbose_level, pri, indent, fmt, args...)   \
  do {                                     \
    if ((verbose_level) <= verbose)        \
      vlog(pri, fmt, ## args);             \
  } while(0)

void log_err(const char *fmt, ...);
void log_msg(const char *fmt, ...);
void log_msg_i(int indent, const char *fmt, ...);

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

// to file only
#define msg_f(verbose_level, fmt, args...)           \
  do {                                               \
    if ((verbose_level) >= verbose && verbose > 0)   \
      log(LOGFILE|LOGINFO, fmt, ## args);            \
  } while(0)

// to tty only
#define msg_tty(verbose_level, fmt, args...)         \
  do {                                               \
    if ((verbose_level) >= verbose && verbose > 0)   \
      log(LOGTTY|LOGINFO, fmt, ## args);             \
  } while(0)


#if ENABLE_TRACE
# define DBGF(fmt, args...)  fprintf(stdout, "%-18s: " fmt, __FUNCTION__ , ## args)
# define DBG(fmt, args...)   fprintf(stdout, fmt, ## args)
#else 
# define DBGF(fmt, args...)  ((void) 0)
# define DBG(fmt, args...)    ((void) 0)
#endif

#define DBGMSG_F DBGF
#define DBGMSG   DBG

#define DBGF_NULL(fmt, args...) ((void) 0)
#define DBGF_F(fmt, args...) fprintf(stdout, "%-18s: " fmt, __FUNCTION__ , ## args)


#endif /* POLDEK_LOG_H */
