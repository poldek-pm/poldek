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

#ifndef EXPORT
# define EXPORT extern
#endif

#include <stdio.h>
#include <stdarg.h>
#include <trurl/tfn_types.h>    /* for tn_fn_free */

#define	LOGTTY	        (1 << 0)	/* log only to TTY output */
#define LOGFILE         (1 << 1)	/* log only to non-TTY output */

#define	LOGERR	        (1 << 5)	/* error conditions */
#define	LOGWARN	        (1 << 6)	/* warning conditions */
#define	LOGINFO         (1 << 7)	/* informational */
#define	LOGNOTICE       (1 << 8)	/* informational */
#define	LOGDEBUG        (1 << 9)	/* debug-level messages */

#define LOGDIE          (1 << 10)   /* abort() */

#define LOGOPT_N        (1 << 11)   /* add "\n" */
#define LOGOPT_CONT     (1 << 12)   /* continuation */

EXPORT int poldek_verbose(void);
EXPORT int poldek_set_verbose(int v);
#ifdef POLDEK_LOG_H_INTERNAL
EXPORT int poldek_VERBOSE;
EXPORT int poldek_TRACE;
#else
EXPORT const int poldek_VERBOSE;
EXPORT const int poldek_TRACE;
#endif

typedef void (*poldek_vlog_fn)(void *, int pri, const char *message);

EXPORT void poldek_log_add_appender(const char *name, void *data, tn_fn_free free,
                             unsigned flags, poldek_vlog_fn dolog);

EXPORT void poldek_log_set_appender(const char *name, void *data, tn_fn_free free,
                             unsigned flags, poldek_vlog_fn dolog);

#define poldek_log_set_default_appender(n, d, ffn) \
         poldek_log_set_appender(n, d, ffn, 0, NULL)

EXPORT void poldek_log_reset_appenders(void);

EXPORT void poldek_log(int pri, const char *fmt, ...)
   __attribute__((format(printf,2,3)));
   
EXPORT void poldek_log_i(int pri, int indent, const char *fmt, ...)
   __attribute__((format(printf,3,4)));
   
EXPORT void poldek_vlog(int pri, int indent, const char *fmt, va_list args);

#ifndef POLDEK_LOG_H_INTERNAL

#define log(pri, fmt, args...) \
       poldek_log(pri, fmt, ## args)

#define logn(pri, fmt, args...) \
       poldek_log(pri | LOGOPT_N, fmt, ## args)

#define log_i(pri, indent, fmt, args...) \
       poldek_log_i(pri, indent, fmt, ## args)
  
#define msg(verbose_level, fmt, args...)   \
  do {                                     \
    if ((verbose_level) <= poldek_VERBOSE) \
      poldek_log(LOGINFO, fmt, ## args);   \
  } while(0)

#define msgn(verbose_level, fmt, args...)        \
  do {                                           \
    if ((verbose_level) <= poldek_VERBOSE)       \
      poldek_log(LOGINFO|LOGOPT_N, fmt, ## args);\
  } while(0)

#define msg_i(verbose_level, indent, fmt, args...)   \
  do {                                               \
    if ((verbose_level) <= poldek_VERBOSE)           \
      log_i(LOGINFO, indent, fmt, ## args);          \
  } while(0)

#define msgn_i(verbose_level, indent, fmt, args...)  \
  do {                                               \
    if ((verbose_level) <= poldek_VERBOSE)           \
      log_i(LOGINFO|LOGOPT_N, indent, fmt, ## args); \
  } while(0)


// to file only
#define msg_f(verbose_level, fmt, args...)           \
  do {                                               \
    if ((verbose_level) <= poldek_VERBOSE)           \
      poldek_log(LOGFILE|LOGINFO, fmt, ## args);     \
  } while(0)

#define msgn_f(verbose_level, fmt, args...)           \
  do {                                                \
    if ((verbose_level) <= poldek_VERBOSE)            \
      poldek_log(LOGFILE|LOGINFO|LOGOPT_N, fmt, ## args);    \
  } while(0)


// to tty only
#define msg_tty(verbose_level, fmt, args...)         \
  do {                                               \
    if ((verbose_level) <= poldek_VERBOSE)           \
      poldek_log(LOGTTY|LOGINFO, fmt, ## args);      \
  } while(0)


// to tty only
#define msgn_tty(verbose_level, fmt, args...)         \
  do {                                                \
    if ((verbose_level) <= poldek_VERBOSE)            \
      poldek_log(LOGTTY|LOGINFO|LOGOPT_N, fmt, ## args);     \
  } while(0)


#define poldek_die(fmt, args...) \
       poldek_log(LOGERR | LOGOPT_N | LOGDIE, fmt, ## args)

#define poldek_die_if(expr, fmt, args...)   \
  ((void) ((expr) ? poldek_log(LOGERR | LOGOPT_N | LOGDIE, fmt, ## args) : 0))

#define poldek_die_ifnot(expr, fmt, args...)   \
  ((void) ((expr) ? 0 : poldek_log(LOGERR | LOGOPT_N | LOGDIE, fmt, ## args)))

EXPORT void poldek_meminf(int vlevel, const char *fmt, ...)
        __attribute__((__format__ (__printf__, 2, 3)));

# define tracef(indent, fmt, args...)                                   \
    do {                                                                \
        if (poldek_TRACE > 0)                                           \
            log_i(LOGDEBUG|LOGOPT_N, indent, "%s() " fmt, __FUNCTION__, ## args); \
    } while(0)
                
# define trace(indent, fmt, args...)                                    \
    do {                                                                \
        if (poldek_TRACE > 0)                                           \
            log_i(LOGDEBUG|LOGOPT_N, indent, fmt, ## args);              \
    } while(0)
    

#if ENABLE_TRACE
# define DBGF(fmt, args...)  fprintf(stdout, "dbg:%-18s: " fmt, __FUNCTION__ , ## args)
# define DBG(fmt, args...)   fprintf(stdout, "dbg:" fmt, ## args)
# define MEMINF(fmt, args...) poldek_meminf(-5, "%-18s: " fmt, __FUNCTION__ , ## args)
# define DBGFIF(cond, fmt, args...) do { if (cond) { fprintf(stdout, "%-18s: " fmt, __FUNCTION__ , ## args); } } while (0)
#else 

static inline int dbgf_noop( const char *fmt, ... )
        __attribute__ ((always_inline))
        __attribute__ ((__format__ (__printf__, 1, 2)));
 
static inline int dbgf_noop( const char *fmt, ... )
{
        return 0;
}

# define DBGF(fmt, args...)	dbgf_noop( "%-18s" fmt, __FUNCTION__, ## args)
# define DBG(fmt, args...)	dbgf_noop( "" fmt, ## args)
# define MEMINF(fmt, args...)	do { dbgf_noop( "%-18s" fmt, __FUNCTION__, ## args); } while (0)
# define DBGFIF(cond, fmt, args...)	do { if (cond) dbgf_noop( "%-18s" fmt, __FUNCTION__, ## args); } while (0)
#endif

#define DBGF_NULL(fmt, args...) ((void) 0)
#define DBGF_F(fmt, args...) fprintf(stdout, "dbg:%-18s: " fmt, __FUNCTION__ , ## args)
#define DBG_F(fmt, args...)   fprintf(stdout, "dbg:" fmt, ## args)
#define MEMINF_F(fmt, args...) poldek_meminf(-5, "%-18s: " fmt, __FUNCTION__ , ## args)
#define DBGFIF_F(cond, fmt, args...) do { if (cond) { fprintf(stdout, "%-18s: " fmt, __FUNCTION__ , ## args); } } while (0)

#endif /* POLDEK_LOG_H_INTERNAL */

#endif /* POLDEK_LOG_H */
