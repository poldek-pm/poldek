

TESTS = test_poldek
noinst_PROGRAMS = test_poldek
test_poldek_SOURCES = test_poldek.c test_match.c test_env.c \
	   	              test_pmdb.c test_op.c test_config.c test.h 	

EXTRA_DIST = utest_mkidx.sh httpd.py

test_poldek_INCLUDES= @CHECK_CFLAGS@
test_poldek_LDADD = @CHECK_LIBS@ ../libpoldek_allsym.la ../vfile/libvfile.la  

poldek_test_conf.conf: ../doc/conf-xml2testconf.xsl ../doc/poldek.conf.xml
	 xsltproc ../doc/conf-xml2testconf.xsl ../doc/poldek.conf.xml > poldek_test_conf.conf

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

