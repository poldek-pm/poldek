#include <malloc.h>
#include "i18n.h"
#include "log.h"

int mem_info_verbose = -1;

void print_mem_info(const char *prefix) 
{
    struct mallinfo mi = mallinfo();
    
    printf("MEMINFO %s: %db via malloc (%d unused, USED %d), %db via mmap\n\n",
           prefix, mi.arena, mi.fordblks, mi.arena - mi.fordblks, mi.hblkhd);
}

void mem_info(int vlevel, const char *prefix) 
{
    if (mem_info_verbose >= vlevel)
        print_mem_info(prefix);
}
