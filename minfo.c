#include <malloc.h>
#include "i18n.h"
#include "log.h"

int mem_info_verbose = 0;

void print_mem_info(const char *prefix) 
{
    struct mallinfo mi = mallinfo();
    msgn(0, "%s: %db via malloc (%d unused), %db via mmap",
        prefix, mi.arena, mi.fordblks,  mi.hblkhd);
}

void mem_info(int vlevel, const char *prefix) 
{
    if (mem_info_verbose >= vlevel)
        print_mem_info(prefix);
}
