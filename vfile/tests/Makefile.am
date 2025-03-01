CPPFLAGS = @CPPFLAGS@ @CHECK_CFLAGS@
LDADD = @CHECK_LIBS@ $(top_builddir)/vfile/libvfile.la

check_PROGRAMS = test_vfile test_vopen3

TESTS = $(check_PROGRAMS)
EXTRA_DIST = test.h

clean-local:
	-rm -f *.tmp core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa*

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
