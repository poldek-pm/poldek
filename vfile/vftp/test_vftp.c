#include <stdio.h>
#include "vftp.h"
#include "ftp.h"

//ftp_verbose = 2;

int test_ftp(void) 
{
    struct ftpcn *cn;
    FILE *stream;

    
    if ((cn = ftpcn_new("smok", 0, "mis", NULL)) == NULL)
        return 0;
        
    stream = fopen("/tmp/dupa.txt", "w");
    ftpcn_retr(cn, fileno(stream), 0, "/welcome.msg", NULL);
    ftpcn_free(cn);
    return 0;
}

int test_vftp(void) 
{
    FILE *stream;
    
    vftp_init(1, NULL);

//    while(1) {
        
        stream = fopen("/tmp/dupa.txt", "w");
        if (!vftp_retr(stream, 0, "ftp://smok/welcome2.msg", NULL))
            printf("retr: %s\n", ftp_errmsg());
        
        fclose(stream);
        
        stream = fopen("/tmp/dupa2.txt", "w");
        if (!vftp_retr(stream, 0,
                       "ftp://localhost/RPMSt/vvgrab-0.15-1.i686.rpm", NULL))
            printf("retr: %s: %m\n", ftp_errmsg());
    
    	
        //printf("Trasfered %ld bytes\n", size);
        fclose(stream);
        
        stream = fopen("/tmp/dupa3.txt", "w");
        if (!vftp_retr(stream, 0, "ftp://smok/welcome2.msg", NULL))
            printf("retr: %s\n", ftp_errmsg());
        
        //printf("Trasfered %ld bytes\n", size);
        fclose(stream);
        
        stream = fopen("/tmp/dupa4.txt", "w");
        
        if (!vftp_retr(stream, 0, "ftp://localhost/welcome2.msg", NULL))
            printf("retr: %s\n", ftp_errmsg());
        
        //printf("Trasfered %ld bytes\n", size);
        fclose(stream);
        //  }
    
    return 0;
}



int main(void) 
{
    //test_ftp();
    test_vftp();
    return 0;
}
