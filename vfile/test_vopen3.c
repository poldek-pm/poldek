#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vopen3.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include "vopen3.h"

int main(int argc, char *argv[])
{
    struct vopen3_st st, st2, *pst;
    int n, ec;
    unsigned p_open_flags = 0;
    char buf[1000];
    char *args[] = { "grep", "dupa", NULL };

    
    
    vopen3_init(&st, "./foo.sh", NULL);
    vopen3_init(&st2, "/usr/bin/less", NULL);
    vopen3_chain(&st, &st2);

    printf("DO EXEC\n");
    vopen3_exec(&st, 0);
    sleep (300);
    printf("DO RUN\n");
    if (write(st.fd_in, "dupa\n", 5) < 0) {
        fprintf(stderr, "write %d: %m\n", st.fd_in);
    }
    
    write(st.fd_in, "duba\n", 5);
    write(st.fd_in, "dupa\n", 5);
    close(st.fd_in);

    
    vopen3_process(&st, 1);
    
    vopen3_close(&st);
    pst = &st;
    while (pst) {
        printf("cmd %s: %s\n", pst->cmd, pst->errmsg);
        pst = pst->next;
    }
    
        
    
    
    printf("OK\n");
    //sleep(300);
    
}

                   
