CPPFLAGS = @CPPFLAGS@ @CHECK_CFLAGS@
LDADD = @CHECK_LIBS@ $(top_builddir)/vfile/libvfile.la

check_PROGRAMS = test_vfile test_vopen3 test_vhttp

TESTS = test_vfile test_vopen3
EXTRA_DIST = test.h http_server.py run_http_tests.sh

# test_vhttp is run via check-http target with server setup
check-local: check-http

clean-local:
	-rm -f *.tmp core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa*

# Run HTTP tests with server setup
check-http: test_vhttp$(EXEEXT)
	$(SHELL) $(srcdir)/run_http_tests.sh ./test_vhttp$(EXEEXT)

runtests: poldek_test_conf.conf
	$(MAKE) check
	@echo; echo "Running *.sh tests"; \
	n=0; nfails=0;                   \
	for i in utest_*.sh; do          \
	    [ -f $$i ] || continue;      \
	    n=$$(expr $$n + 1);          \
	    echo "  running $$i";        \
	    sh $$i;                      \
        if [ $$? -ne 0 ]; then       \
          nfails=$$(expr $$nfails + 1); \
          echo "    $$i FAILED";     \
        else                         \
          echo "    $$i PASSED";     \
        fi;                          \
   done;                             \
   echo "====================================="; \
   echo "$$n tests passed / $$nfails failed";    \
   echo "====================================="
