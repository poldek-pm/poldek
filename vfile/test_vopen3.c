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

int proc(void *dummy)
{
    char line[1024];
    int n = 1024;

    (void)dummy;

    char *fgets(char *s, int size, FILE *stream);
    while (fgets(line, n, stdin)) {
        fprintf(stdout, "%s", line);
        fprintf(stdout, "xx = %s", line);
    }
    return 0;
}



int main(void)
{
    struct vopen3_st st, st2, st3, st4, *pst;
    //int n, ec;
    //unsigned p_open_flags = 0;
    //char buf[1000];
    //char *args[] = { "awk", "'{print $1}'", NULL };



    vopen3_init(&st, "./foo.sh", NULL);
    vopen3_init(&st2, "/bin/cat", NULL);
    //vopen3_chain(&st, &st2);

    vopen3_init_fn(&st3, proc, NULL);
    //vopen3_chain(&st, &st3);

    vopen3_init(&st4, "/usr/bin/less", NULL);
    //vopen3_chain(&st, &st4);

    printf("DO EXEC\n");
    vopen3_exec(&st, 0);
    //sleep (300);
    printf("DO RUN %d\n", st.fd_in);
    if (write(st.fd_in, "dupa\n", 5) < 0) {
        fprintf(stderr, "write %d: %m\n", st.fd_in);
    }

    write(st.fd_in, "duba\n", 5);
    write(st.fd_in, "dupa\n", 5);
    vopen3_process(&st, 1);
    write(st.fd_in, "dupa\n", 5);
    vopen3_close(&st);
    pst = &st;
    while (pst) {
        printf("cmd %s: %s\n", pst->cmd, pst->errmsg);
        pst = pst->next;
    }




    printf("OK\n");
    //sleep(300);

}
