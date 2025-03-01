#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include "../vopen3.h"
#include "test.h"

char *fgets(char *s, int size, FILE *stream);

int proc(void *nthptr)
{
    char line[1024], nbuf[8];
    int n = 1024;
    int nth = *((int *)nthptr);

    while (fgets(line, n, stdin)) {
        nth++;
        fprintf(stdout, "%s", line);
        n_snprintf(nbuf, sizeof(nbuf), "%d\n", nth);
        fail_if(strcmp(nbuf, line) != 0, "%s != %s", line, nbuf);


        if (nth == 4)
            break;
    }

    return 0;
}


START_TEST(test_vopen3)
{
    int nth = 0;
    struct vopen3_st st, st2, *pst;

    vopen3_init(&st, "/bin/cat", NULL);
    vopen3_init_fn(&st2, proc, &nth);
    vopen3_chain(&st, &st2);

    vopen3_exec(&st, 0);
    if (write(st.fd_in, "1\n", 2) < 0) {
        fprintf(stderr, "write %d: %m\n", st.fd_in);
    }
    if (write(st.fd_in, "2\n", 2) != 5)
        fprintf(stderr, "write %d: %m\n", st.fd_in);

    if (write(st.fd_in, "3\n", 2) != 2)
        fprintf(stderr, "write %d: %m\n", st.fd_in);

    if (write(st.fd_in, "4\n", 2) != 2)
        fprintf(stderr, "write %d: %m\n", st.fd_in);

    vopen3_process(&st, 1);

    close(st.fd_in);
    vopen3_close(&st);
    pst = &st;
    while (pst) {
        fail_if(pst->errmsg != NULL, "%s: error %s\n", pst->cmd, pst->errmsg);
        pst = pst->next;
    }
    //fail_if(nth != 4, "nth %d != 4\n", nth);
}

NTEST_RUNNER("vopen3", test_vopen3);
