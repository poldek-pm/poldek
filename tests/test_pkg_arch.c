/*
  A fork() waits for an arch being registered by another thread: the child
  gets the list of arches whole, with that arch in it, and registers and
  reads its own. So whether threads load sources or not.
*/
#include "test.h"
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "poldek.h"
#include "thread.h"

static const char *thread_arch;
static int hold_once;           /* the next score waits for go[] */
static int scores;              /* asked for: one for each new arch */
static int entered[2], go[2];

/* called by pkg_set_arch() of a new arch, in the middle of its registration */
int rpmMachineScore(int type, const char *name)
{
    char c;

    (void)type;
    (void)name;
    __atomic_add_fetch(&scores, 1, __ATOMIC_SEQ_CST);
    if (__atomic_exchange_n(&hold_once, 0, __ATOMIC_SEQ_CST)) {
        if (write(entered[1], "e", 1) != 1 || read(go[0], &c, 1) != 1)
            return 0;
    }
    return 1;
}

#ifdef __GLIBC__
static int atfork_fail_once;    /* the next pthread_atfork() fails */

extern int __register_atfork(void (*prepare)(void), void (*parent)(void),
                             void (*child)(void), void *dso_handle);
extern void *__dso_handle;

/* the library's call; glibc's own pthread_atfork() is this one call too */
int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
    if (atfork_fail_once) {
        atfork_fail_once = 0;
        return ENOMEM;
    }
    return __register_atfork(prepare, parent, child, __dso_handle);
}

/* a failed pthread_atfork() leaves the library set up, with a warning;
   first of the tests: with CK_FORK=no the handlers stay registered */
START_TEST(test_init_after_atfork_error) {
    atfork_fail_once = 1;
    expect_int(poldeklib_init(), 1);
    expect_int(atfork_fail_once, 0);
    expect_int(poldeklib_init(), 1);
    poldeklib_destroy();
}
END_TEST
# define INIT_TESTS test_init_after_atfork_error,
#else
# define INIT_TESTS
#endif

static void *register_arch(void *pkg)
{
    pkg_set_arch(pkg, thread_arch);
    return pkg;
}

static void *go_after_a_second(void *arg)
{
    sleep(1);
    return write(go[1], "g", 1) == 1 ? arg : NULL;
}

/* arches of its own for each test: with CK_FORK=no they share the list */
static void fork_while_thread_registers_arch(bool threads_load, const char *arch,
                                             const char *child_arch)
{
    struct pkg of_thread, same, of_child;
    struct pollfd p;
    pthread_t thread, timer;
    pid_t child;
    int st;

    memset(&of_thread, 0, sizeof(of_thread));
    memset(&same, 0, sizeof(same));
    memset(&of_child, 0, sizeof(of_child));
    fail_if(pipe(entered) != 0 || pipe(go) != 0, "pipe: %s", strerror(errno));
    fail_if(!poldeklib_init(), "poldeklib_init() failed");
    if (threads_load)
        poldek_threading_toggle(true);

    /* the thread stops in the score of its arch, the first one registered,
       until the timer lets it go on */
    thread_arch = arch;
    hold_once = 1;
    fail_if(pthread_create(&thread, NULL, register_arch, &of_thread) != 0, "pthread_create");
    p.fd = entered[0];
    p.events = POLLIN;
    fail_if(poll(&p, 1, 10000) != 1, "the thread registered no arch");
    fail_if(pthread_create(&timer, NULL, go_after_a_second, NULL) != 0, "pthread_create");

    if ((child = fork()) == 0) {
        int asked;

        signal(SIGALRM, SIG_DFL);   /* check's handler would kill the test */
        alarm(2);
        scores = 0;
        pkg_set_arch(&same, arch);
        asked = scores;             /* 0: it was registered before the fork */
        pkg_set_arch(&of_child, child_arch);
        if (asked != 0)
            _exit(1);
        _exit(strcmp(pkg_arch(&same), arch) != 0 ||
              strcmp(pkg_arch(&of_child), child_arch) != 0 ? 2 : 0);
    }
    fail_if(child < 0, "fork: %s", strerror(errno));
    fail_if(waitpid(child, &st, 0) != child, "waitpid: %s", strerror(errno));
    fail_if(WIFSIGNALED(st) && WTERMSIG(st) == SIGALRM, "the child hung registering an arch");
    fail_if(WIFEXITED(st) && WEXITSTATUS(st) == 1,
            "the child was forked in the middle of the registration of an arch");
    fail_if(!WIFEXITED(st) || WEXITSTATUS(st) != 0, "the child got wrong arches");

    pthread_join(timer, NULL);
    pthread_join(thread, NULL);
    if (threads_load)
        poldek_threading_toggle(false);
    expect_str(pkg_arch(&of_thread), arch);
    poldeklib_destroy();
}

START_TEST(test_fork_while_thread_registers_arch) {
    fork_while_thread_registers_arch(false, "arch-of-a-thread", "arch-of-a-child");
}
END_TEST

START_TEST(test_fork_while_loading_thread_registers_arch) {
    fork_while_thread_registers_arch(true, "arch-of-a-loading-thread",
                                     "arch-of-its-child");
}
END_TEST

NTEST_RUNNER("pkg_arch", INIT_TESTS test_fork_while_thread_registers_arch,
             test_fork_while_loading_thread_registers_arch);
