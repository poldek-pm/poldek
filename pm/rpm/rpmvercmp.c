#ifdef HAVE_CONFIG_H
# include "config.h"
#endif 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpm/rpmlib.h>

#include "i18n.h"

#ifndef HAVE__RPMVERCMP /* _rpmvercmp - new in rpm 4.4.7 */
extern int rpmvercmp(const char *one,  const char *two);
#endif 


int main(int argc, char *argv[])
{
    int cmprc, is_test = 0;
    const char *v1, *v2;

    
    if (argc < 3) {
        printf("Usage: rpmvercmp [-t] VERSION1 VERSION2\n");
        exit(EXIT_SUCCESS);
    }

    if (argc == 3) {
        v1 = argv[1];
        v2 = argv[2];
        
    } else if (argc == 4 && strcmp(argv[1], "-t") == 0) {
        v1 = argv[2];
        v2 = argv[3];
        is_test = 1;
        
    } else {
        printf("Usage: rpmvercmp [-t] VERSION1 VERSION2\n");
        exit(1);
    }

    
    cmprc = rpmvercmp(v1, v2);
    printf("%s %s %s\n", v1, cmprc == 0 ?  "==" : cmprc > 0 ? ">" : "<", v2);
    if (is_test) {
        cmprc = rpmvercmp(v2, v1);
        printf("%s %s %s\n", v2, cmprc == 0 ?  "==" : cmprc > 0 ? ">" : "<", v1);
    }
    
    if (cmprc < 0)
        cmprc = 2;
    exit(cmprc);
}
