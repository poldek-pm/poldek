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
    struct vf_request *req;
    int verbose = 1;
    
    vftp_init(&verbose, NULL);

    req = vf_request_new("/tmp/dupa.txt", "ftp://smok/welcome2.msg");
    if (!vftp_retr(req))
        printf("retr: %s\n", vftp_errmsg());
    
    vf_request_free(req);
    return 0;
}



int main(void) 
{
    //test_ftp();
    test_vftp();
    return 0;
}
