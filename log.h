/* $Id$ */
#ifndef POLDEK_LOG_H
#define POLDEK_LOG_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef __GNUC__
#  undef __attribute__
#  define __attribute__(x) /*nothing*/                                      
#endif   

#include <stdio.h>
#include <stdarg.h>

#define	LOGTTY	        (1 << 0)	/* log only to TTY output */
#define LOGFILE         (1 << 1)	/* log only to non-TTY output */

#define	LOGERR	        (1 << 5)	/* error conditions */
#define	LOGWARN	        (1 << 6)	/* warning conditions */
#define	LOGINFO         (1 << 7)	/* informational */
#define	LOGNOTICE       (1 << 8)	/* informational */
#define	LOGDEBUG	(1 << 9)	/* debug-level messages */

#define LOGDIE          (1 << 10)

#define LOGOPT_N        (1 << 11)       /* add "\n" */

extern int verbose;

int log_init(const char *pathname, FILE *tty, const char *prefix);
void log_closelog(void);
int log_enabled_filelog(void);

FILE *log_stream(void);
FILE *log_file_stream(void);


void poldek_log(int pri, const char *fmt, ...)
   __attribute__((format(printf,2,3)));
   
void poldek_log_i(int pri, int indent, const char *fmt, ...)
   __attribute__((format(printf,3,4)));
   
void poldek_vlog(int pri, int indent, const char *fmt, va_list args);

void log_err(const char *fmt, ...)
  __attribute__((format(printf,1,2)));
void log_msg(const char *fmt, ...)
  __attribute__((format(printf,1,2))); 
void log_msg_i(int indent, const char *fmt, ...)
  __attribute__((format(printf,2,3)));
void log_tty(const char *fmt, ...)  
  __attribute__((format(printf,1,2)));

#ifndef POLDEK_LOG_H_INTERNAL  

#define log(pri, fmt, args...) \
       poldek_log(pri, fmt, ## args)

#define logn(pri, fmt, args...) \
       poldek_log(pri | LOGOPT_N, fmt, ## args)

#define log_i(pri, indent, fmt, args...) \
       poldek_log_i(pri, indent, fmt, ## args)

#define vlog(pri, fmt, args...) \
       poldek_vlog(pri, fmt, ## args)

#define log_debug(fmt, args...) \
  poldek_log(LOG_DEBUG, "%s: " fmt, __FUNCTION__ , ## args)

#define logv(verbose_level, pri, indent, fmt, args...)   \
  do {                                     \
    if ((verbose_level) <= verbose)        \
      poldek_vlog(pri, fmt, ## args);             \
  } while(0)

  
#define msg(verbose_level, fmt, args...)   \
  do {                                     \
    if ((verbose_level) <= verbose)        \
      poldek_log(LOGINFO, fmt, ## args);       \
  } while(0)

#define msgn(verbose_level, fmt, args...)        \
  do {                                           \
    if ((verbose_level) <= verbose)              \
      poldek_log(LOGINFO|LOGOPT_N, fmt, ## args);       \
  } while(0)

#define msg_i(verbose_level, indent, fmt, args...)   \
  do {                                               \
    if ((verbose_level) <= verbose)                  \
      log_i(LOGINFO, indent, fmt, ## args);          \
  } while(0)

#define msgn_i(verbose_level, indent, fmt, args...)  \
  do {                                               \
    if ((verbose_level) <= verbose)                  \
      log_i(LOGINFO|LOGOPT_N, indent, fmt, ## args); \
  } while(0)


// to file only
#define msg_f(verbose_level, fmt, args...)           \
  do {                                               \
    if ((verbose_level) <= verbose)                  \
      poldek_log(LOGFILE|LOGINFO, fmt, ## args);            \
  } while(0)

#define msgn_f(verbose_level, fmt, args...)           \
  do {                                                \
    if ((verbose_level) <= verbose)                   \
      poldek_log(LOGFILE|LOGINFO|LOGOPT_N, fmt, ## args);    \
  } while(0)


// to tty only
#define msg_tty(verbose_level, fmt, args...)         \
  do {                                               \
    if ((verbose_level) <= verbose)                  \
      poldek_log(LOGTTY|LOGINFO, fmt, ## args);             \
  } while(0)


// to tty only
#define msgn_tty(verbose_level, fmt, args...)         \
  do {                                                \
    if ((verbose_level) <= verbose)                   \
      poldek_log(LOGTTY|LOGINFO|LOGOPT_N, fmt, ## args);     \
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

#define dbgf(fmt, args...)  fprintf(stdout, "%-18s: " fmt, __FUNCTION__ , ## args)
#define dbgf_(fmt, args...) ((void) 0)

#endif /* POLDEK_LOG_H_INTERNAL */
#endif /* POLDEK_LOG_H */
