#include <stdio.h>
#include <stdlib.h>
#include <rpm/rpmlib.h>

#include "rpmadds.h"

int main(int argc, char *argv[])
{
    int cmprc;
    
    if (argc != 3) {
        printf("Usage: rpmvercmp VERSION1 VERSION2\n");
        exit(EXIT_SUCCESS);
    }

    cmprc = rpmvercmp(argv[1], argv[2]);
    printf("%s %s %s\n", argv[1], cmprc == 0 ?  "==" : cmprc > 0 ? ">" : "<",
           argv[2]);
    if (cmprc < 0)
        cmprc = 2;
    exit(cmprc);
}
