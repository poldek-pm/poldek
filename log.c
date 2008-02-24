/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include "i18n.h"
#include "poldek_term.h"
#define POLDEK_LOG_H_INTERNAL
#include "log.h"

int poldek_VERBOSE = 0;
int poldek_TRACE = -1;

static int default_say_goodbye(const char *msg);
int (*poldek_log_say_goodbye)(const char *msg) = default_say_goodbye;

static void do_log(unsigned flags, int pri, const char *fmt, va_list args);
static void vlog_file(void *stream, int pri, const char *message);
static void vlog_tty(void *foo, int pri, const char *message);


struct poldek_log_appender {
    int flags;                  /* LOGTTY for TTY-output */
    void *_data;
    void (*dolog)(void *, int pri, const char *message);
    void (*free)(void *);
    char name[0];
};

static tn_array *log_appenders = NULL;


void poldek_log_reset_appenders(void) 
{
    n_array_cfree(&log_appenders);
}


static void appender_free(struct poldek_log_appender *ape)
{
    if (ape->free)
        ape->free(ape->_data);
}

void poldek_log_add_appender(const char *name, void *data, tn_fn_free free,
                             unsigned flags, poldek_vlog_fn dolog)
{
    struct poldek_log_appender *ape;
    
    if (log_appenders == NULL)
        log_appenders = n_array_new(4, (tn_fn_free)appender_free, NULL);

    if (n_str_eq(name, "_FILE")) {
        dolog = vlog_file;
        flags |= LOGFILE;
        
    } else if (n_str_eq(name, "_TTY")) {
        dolog = vlog_tty;
        flags |= LOGTTY;
    }

    if (flags == 0)
        flags |= LOGTTY;

    n_assert(dolog);
    ape = n_calloc(1, sizeof(*ape) + strlen(name) + 1);
    
    memcpy(ape->name, name, strlen(name) + 1);
    ape->flags = flags;
    ape->_data = data;
    ape->dolog = dolog;
    ape->free = free;
    n_array_push(log_appenders, ape);
}

void poldek_log_set_appender(const char *name, void *data, tn_fn_free free,
                             unsigned flags, poldek_vlog_fn dolog)
{
    if (log_appenders != NULL)
        n_array_clean(log_appenders);

    poldek_log_add_appender(name, data, free, flags, dolog);
}

static int default_say_goodbye(const char *msg)
{
    msg = msg; /* do nothing, msg is logged before die */
    return 1;
}

int poldek_verbose(void)
{
    return poldek_VERBOSE;
}

int poldek_set_verbose(int v)
{
    const char *p;
    
    int vv = poldek_VERBOSE;
    poldek_VERBOSE = v;

    if ((p = getenv("POLDEK_TRACE")) && *p && *p != '0')
        poldek_TRACE = 1;

    return vv;
}


void poldek_log(int pri, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    poldek_vlog(pri, 0, fmt, args);
    va_end(args);
}

void poldek_log_i(int pri, int indent, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    poldek_vlog(pri, indent, fmt, args);
    va_end(args);
}


void poldek_vlog(int pri, int indent, const char *fmt, va_list args)
{
    static int last_endlined = 1;
    char buf[1024], tmp_fmt[1024];
    int  buf_len = 0, fmt_len = 0, flags, is_continuation = 0, is_endlined = 0;
    
    if (*fmt == '_') {
        fmt++;
        is_continuation = 1;
        
    } else if (*fmt == '\n') {
        buf[buf_len++] = '\n';
        fmt++;
        is_endlined = 1;
    }

    if (*fmt) {
        fmt_len = strlen(fmt);
        is_endlined = (fmt[fmt_len - 1] == '\n');
        
        if ((pri & LOGOPT_N) && !is_endlined && (int)sizeof(tmp_fmt) > fmt_len + 2) {
            memcpy(tmp_fmt, fmt, fmt_len);
            tmp_fmt[fmt_len++] = '\n';
            tmp_fmt[fmt_len] = '\0';
            fmt = tmp_fmt;
            is_endlined = 1;
        }
    }

    /* auto line break for errors and warnings */
        
    if (pri & LOGTTY) {
        if (!last_endlined && !is_continuation && (pri & (LOGERR|LOGWARN))) {
            buf[buf_len++] = '\n';
        }
        last_endlined = is_endlined;
    }

    
    if (indent > 0 && (unsigned)indent < sizeof(buf) - buf_len - 2) {
        memset(&buf[buf_len], ' ', indent);
        buf_len += indent;
    }
    
    buf[buf_len] = '\0';

#if 0                           /* debug */
    if (pri & LOGTTY) {
        char s[256];
        n_snprintf(s, sizeof(s), "l [%s] [%s]", buf, fmt);
        vfprintf(stderr, s, args);
    }
#endif    
    if (*fmt == '\0') {
        fmt = buf;
        
    } else if (*buf != '\0') {
        int newfmt_len = buf_len + fmt_len;
        char *newfmt = alloca(newfmt_len + 1);
        
        memcpy(newfmt, buf, buf_len);
        memcpy(&newfmt[buf_len], fmt, fmt_len);
        newfmt[newfmt_len] = '\0';
        
        fmt = newfmt;
        fmt_len = newfmt_len;
    }

    /* revert LOG[TTY|FILE]  */
    flags = LOGTTY | LOGFILE;
    if (pri & LOGTTY)
        flags &= ~LOGFILE;
    
    else if (pri & LOGFILE)
        flags &= ~LOGTTY;

    if (is_continuation)
        pri |= LOGOPT_CONT;

    do_log(flags, pri, fmt, args);

    if (pri & LOGDIE) {
        char msg[1024];
        n_snprintf(msg, sizeof(msg), fmt, args);
        if (poldek_log_say_goodbye(msg))
            abort();
    }
}


static void vlog_tty(void *foo, int pri, const char *message)
{
    char buf[44];
    int n = 0;

    foo = foo;
    if (pri & LOGERR)
        n = poldek_term_snprintf_c(PRCOLOR_RED | PRAT_BOLD, buf, sizeof(buf),
                                   _("error: "));
    
    else if (pri & LOGWARN)
        n = poldek_term_snprintf_c(PRCOLOR_RED | PRAT_BOLD, buf, sizeof(buf),
                                   _("warn: "));

    else if (pri & LOGNOTICE)
        n = poldek_term_snprintf_c(PRCOLOR_YELLOW | PRAT_BOLD, buf, sizeof(buf),
                                   _("notice: "));

    else if (pri & LOGDEBUG)
        n = n_snprintf(buf, sizeof(buf), ":");
    
            
    if (n > 0)
        fprintf(stdout, "%s", buf);

    fprintf(stdout, "%s", message);
    fflush(stdout);
}

static void vlog_file(void *stream, int pri, const char *message)
{
    
    if ((pri & LOGOPT_CONT) == 0) {
        char timbuf[64];
        time_t t;
            
        t = time(NULL);
        strftime(timbuf, sizeof(timbuf), "%Y.%m.%d %H:%M:%S ", localtime(&t));
        fprintf(stream, "%s", timbuf);
    }
    
    if (pri & LOGERR)
        fprintf(stream, "%s", _("error: "));
    
    else if (pri & LOGWARN)
        fprintf(stream, "%s", _("warn: "));
            
    fprintf(stream, "%s", message);
    fflush(stream);
}

static void do_log(unsigned flags, int pri, const char *fmt, va_list args) 
{
    char message[16 * 1024], *endl = NULL;
    int i;

    if (*fmt == '\n' && (pri & (LOGERR|LOGWARN|LOGNOTICE))) {
        fmt++;
        endl = "\n";
    }

    n_vsnprintf(message, sizeof(message), fmt, args);
    
    if (log_appenders == NULL || n_array_size(log_appenders) == 0) {
        if (endl)
            vlog_tty(NULL, LOGOPT_CONT, endl);
            
        vlog_tty(NULL, pri, message);
        return;
    }

    for (i=0; i < n_array_size(log_appenders); i++) {
        struct poldek_log_appender *ape = n_array_nth(log_appenders, i);

        if ((ape->flags & flags) == 0)
            continue;
        
        if (ape->dolog) {
            if (endl)
                ape->dolog(ape->_data, LOGOPT_CONT, endl);
            ape->dolog(ape->_data, pri, message);
        
        } else {
            fprintf(stderr, "appender without dolog()?\n");
            n_assert(0);
        }
    }
}


