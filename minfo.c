#include <malloc.h>
#include "i18n.h"
#include "log.h"

int mem_info_verbose = -1;

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

void mem_info(int vlevel, const char *fmt, ...)
{
    va_list args;
    if (mem_info_verbose >= vlevel) {
        va_start(args, fmt);
        print_mem_info(fmt, args); 
        va_end(args);
    }
}
