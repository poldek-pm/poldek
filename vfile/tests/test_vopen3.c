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
        fail_if(strcmp(nbuf, line) != 0);


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
        fail_if(pst->errmsg != NULL);
        pst = pst->next;
    }
    //fail_if(nth != 4, "nth %d != 4\n", nth);
}

static char chain_output_buf[4096];
static size_t chain_output_len = 0;

static int grab_output(const char *buf, void *arg)
{
    (void)arg;
    size_t len = strlen(buf);
    if (chain_output_len + len < sizeof(chain_output_buf) - 1) {
        memcpy(chain_output_buf + chain_output_len, buf, len);
        chain_output_len += len;
        chain_output_buf[chain_output_len] = '\0';
    }
    return 0;
}

static int proc_chain(void *dummy)
{
    char line[1024];
    int n = 1024;

    (void)dummy;

    while (fgets(line, n, stdin)) {
        fprintf(stdout, "proc: %s", line);
    }
    return 0;
}

START_TEST(test_vopen3_chain)
{
    struct vopen3_st st, st2, st3, *pst;

    /* Reset output buffer */
    memset(chain_output_buf, 0, sizeof(chain_output_buf));
    chain_output_len = 0;

    /* Chain: grep.sh | cat | proc_chain */
    vopen3_init(&st, "./grep.sh", NULL);
    vopen3_init(&st2, "/bin/cat", NULL);
    vopen3_init_fn(&st3, proc_chain, NULL);

    /* Set up grab function to capture output from last command */
    vopen3_set_grabfn(&st3, grab_output, NULL);

    /* Chain the commands: st -> st2 -> st3 */
    vopen3_chain(&st, &st2);
    vopen3_chain(&st2, &st3);

    /* Execute the pipeline with VOPEN3_PIPESTDOUT to capture output */
    vopen3_exec(&st, VOPEN3_PIPESTDOUT);
    fail_if(st.fd_in < 0);

    /* Write test data to the pipeline */
    if (write(st.fd_in, "line1\n", 6) < 0)
        fprintf(stderr, "write failed: %m\n");

    if (write(st.fd_in, "line2\n", 6) < 0)
        fprintf(stderr, "write failed: %m\n");

    /* Close stdin to signal EOF */
    close(st.fd_in);
    st.fd_in = -1;

    /* Wait for pipeline to complete */
    vopen3_process(&st, 1);
    vopen3_close(&st);

    /* Verify output content - check that grep.sh output and proc_chain works */
    /* Expected: grep.sh adds "grep.sh:", proc_chain adds "proc:" */
    fail_if(strstr(chain_output_buf, "proc: grep.sh: line1") == NULL);
    fail_if(strstr(chain_output_buf, "proc: grep.sh: line2") == NULL);
    fail_if(strstr(chain_output_buf, "proc: grep.sh: exit!") == NULL);

    /* Check all commands in chain completed without errors */
    pst = &st;
    while (pst) {
        if (pst->errmsg)
            printf("cmd %s: %s\n", pst->cmd ? pst->cmd : "(fn)", pst->errmsg);
        fail_if(pst->errmsg != NULL);
        pst = pst->next;
    }
}

NTEST_RUNNER("vopen3", test_vopen3, test_vopen3_chain);
