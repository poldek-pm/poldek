/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_MALLOPT
# include <malloc.h>
#endif

#include "compiler.h"
#include "i18n.h"
#include "log.h"

static int mem_info_verbose = 1;

static char *nbytes2str(char *buf, int bufsize, unsigned long nbytes) 
{
    char unit = 'B';
    double nb;

    nb = nbytes;
    
    if (nb > 1024) {
        nb /= 1024;
        unit = 'K';
    }
    
    if (nb > 1024) {
        nb /= 1024;
        unit = 'M';
    }

    snprintf(buf, bufsize, "%.2f%c", nb, unit);
    return buf;
}

#ifdef HAVE_MALLOPT
static
void print_mem_info(const char *fmt, va_list args) 
{
    struct mallinfo mi = mallinfo();
    char buf[32], barena[32], bford[32], bmmap[32], bunused[32];
    
    nbytes2str(buf, sizeof(buf), mi.arena - mi.fordblks + mi.hblkhd); 

    vfprintf(stderr, fmt, args);
    fprintf(stderr, ": %s total: %s malloc (%s un, %s used), %s mmap\n",
            buf, nbytes2str(barena, 32, mi.arena),
            nbytes2str(bford, 32, mi.fordblks),
            nbytes2str(bunused, 32, mi.arena - mi.fordblks),
            nbytes2str(bmmap, 32, mi.hblkhd));
}

void poldek_meminf(int vlevel, const char *fmt, ...)
{
    va_list args;
    if (mem_info_verbose >= vlevel) {
        va_start(args, fmt);
        print_mem_info(fmt, args); 
        va_end(args);
    }
}
#endif /* HAVE_MALLOPT */
