#include <stdio.h>
#include "vhttp.h"
#include "http.h"

//http_verbose = 2;

int test_http(void) 
{
    struct httpcn *cn;
    FILE *stream;

    
    if ((cn = httpcn_new("smok", 0, NULL, NULL)) == NULL)
        return 0;
        
    stream = fopen("/tmp/dupa.txt", "w");
    httpcn_retr(cn, fileno(stream), 0, "/welcome.msg", NULL);
    httpcn_free(cn);
    return 0;
}

int test_vhttp(void) 
{
    FILE *stream;
    int verbose = 10;
    long off;
    
    vhttp_init(&verbose, NULL);
    
//    while(1) {
    
    stream = fopen("/tmp/dupa.txt", "a");
    off = ftell(stream);
    printf("\n\nFROM %ld\n", off);
    if (!vhttp_retr(stream, off, "http://localhost/PLD/ddd-3.3.1-7.i686.rpm", NULL))
        printf("retr: %s\n", vhttp_errmsg());

    fclose(stream);
    stream = fopen("/tmp/dupa.txt", "a");
    off = ftell(stream);
    printf("\n\nFROM %ld\n", off);
    if (!vhttp_retr(stream, off, "http://localhost/PLD/ddd-3.3.1-7.i686.rpm", NULL))
        printf("retr: %s\n", vhttp_errmsg());
        
    fclose(stream);

        
    return 0;
        
        
        stream = fopen("/tmp/dupa2.txt", "w");
        if (!vhttp_retr(stream, 0,
                       "http://smok/RPMSt/vvgrab-0.15-1.i686.rpm", NULL))
            printf("retr: %s\n", vhttp_errmsg());
    
    	
        //printf("Trasfered %ld bytes\n", size);
        fclose(stream);
        return 0;
        
        stream = fopen("/tmp/dupa3.txt", "w");
        if (!vhttp_retr(stream, 0, "http://smok/welcome2.msg", NULL))
            printf("retr: %s\n", vhttp_errmsg());
        
        //printf("Trasfered %ld bytes\n", size);
        fclose(stream);
        
        stream = fopen("/tmp/dupa4.txt", "w");
        
        if (!vhttp_retr(stream, 0, "http://localhost/welcome2.msg", NULL))
            printf("retr: %s\n", vhttp_errmsg());
        
        //printf("Trasfered %ld bytes\n", size);
        fclose(stream);
        //  }
    
    return 0;
}



int main(void) 
{
    //test_http();
    test_vhttp();
    return 0;
}
