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


void print_mem_info(const char *prefix) 
{
    struct mallinfo mi = mallinfo();
    char buf[32], barena[32], bford[32], bmmap[32], bunused[32];
    
    nbytes2str(buf, sizeof(buf), mi.arena - mi.fordblks + mi.hblkhd); 
    
    printf("MEMINFO %s: %s %s via malloc (%s unused, %s used), %s via mmap\n\n",
           prefix, buf,
           nbytes2str(barena, 32, mi.arena),
           nbytes2str(bford, 32, mi.fordblks),
           nbytes2str(bunused, 32, mi.arena - mi.fordblks),
           nbytes2str(bmmap, 32, mi.hblkhd));
}

void mem_info(int vlevel, const char *prefix) 
{
    if (mem_info_verbose >= vlevel)
        print_mem_info(prefix);
}
