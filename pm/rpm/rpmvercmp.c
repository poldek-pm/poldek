#ifdef HAVE_CONFIG_H
# include "config.h"
#endif 

#include <stdio.h>
#include <stdlib.h>

#define _RPMEVR_INTERNAL
#include <rpm/rpmevr.h>

static void parse(const char *evrstr, EVR_t evr)
{
    rpmEVRparse(evrstr, evr);
    
    if (evr->E == NULL) {
	evr->E = "0";
	evr->Elong = 0;
    }
    
    if (evr->R == NULL)
	evr->R = "0";
}

int main(int argc, char *argv[])
{
    int cmprc;
    const char *v1, *v2;
    EVR_t evr1, evr2;
    
    if (argc < 3) {
        printf("Usage: rpmvercmp VERSION1 VERSION2\n");
        exit(EXIT_SUCCESS);
    }

    if (argc == 3) {
        v1 = argv[1];
        v2 = argv[2];
    
    } else {
        printf("Usage: rpmvercmp VERSION1 VERSION2\n");
        exit(1);
    }

    evr1 = malloc(sizeof(struct EVR_s));
    evr2 = malloc(sizeof(struct EVR_s));
    
    parse(v1, evr1);
    parse(v2, evr2);
    
    cmprc = rpmEVRcompare(evr1, evr2);
    
    printf("%s %s %s\n", v1, cmprc == 0 ?  "==" : cmprc > 0 ? ">" : "<", v2);
    
    if (cmprc < 0)
        cmprc = 2;

    free((char *)evr1->str);
    free((char *)evr2->str);
    free(evr1);
    free(evr2);

    exit(cmprc);
}
