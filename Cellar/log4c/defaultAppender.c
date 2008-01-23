// $Id$
// Copyright (c) 2001, Bit Farm, Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. The name of the author may not be used to endorse or promote products
//    derived from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "log4c.h"
#include <stdio.h>

/**
 * The root category's default logging function.
 */

static char *priorityNames[] = {
    "Zero Priority",
    "TRACE",
    "DEBUG",
    "INFO",
    "NOTICE",
    "WARNING",
    "ERROR",
    "CRITICAL ERROR",
    "ALERT",
    "EMERGENCY",
};

static void doAppend(struct LogAppender* this, struct LogEvent* ev);

static struct DefaultLogAppender {
    struct LogAppender appender;
    FILE *file;
} defaultLogAppender = { { doAppend }, NULL } ;

struct LogAppender* log_defaultLogAppender  = &defaultLogAppender.appender;

static void doAppend(struct LogAppender* this0, struct LogEvent* ev) {

    // TODO: define a format field in struct for timestamp, etc.
    char *pn;
    char buf[20];
    struct DefaultLogAppender* this = (struct DefaultLogAppender*)this0;
    
    if (this->file == NULL) this->file = stderr;
    
    if (ev->priority < 0) {
        pn = "Negative Priority NOT ALLOWED!!";
    }
    else if (ev->priority < sizeof(priorityNames)) {
        pn = priorityNames[ev->priority];
    } else {
        sprintf(buf, "%s+%d",
                priorityNames[sizeof(priorityNames)-1],
                ev->priority - sizeof(priorityNames) + 1);
    }
    fprintf(stderr, "%-7s ", pn);
    fprintf(stderr, "%s:%d: ", ev->fileName, ev->lineNum);
    vfprintf(stderr, ev->fmt, ev->ap);
    fprintf(stderr, "\n");
}
