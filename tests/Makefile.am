

TESTS = test_poldek
noinst_PROGRAMS = test_poldek
test_poldek_SOURCES = test_poldek.c test_match.c test_env.c \
	   	              test_pmdb.c test_op.c test_config.c test.h 	

EXTRA_DIST = poldek_test_conf.conf sh


test_poldek_INCLUDES= @CHECK_CFLAGS@
test_poldek_LDADD = @CHECK_LIBS@ ../libpoldek_allsym.la ../vfile/libvfile.la  

poldek_test_conf.conf: ../doc/conf-xml2testconf.xsl ../doc/poldek.conf.xml
	 xsltproc ../doc/conf-xml2testconf.xsl ../doc/poldek.conf.xml > poldek_test_conf.conf

clean-local:
	-rm -f *.tmp core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa* 
	-find sh -name \*~ | xargs -r rm

check: poldek_test_conf.conf
	$(MAKE) check-am
	-rm -f poldek_test_conf.*

sheck:
	@echo; echo "Running *.sh tests";  \
	find sh -name \*~ | xargs -r rm;   \
	n=0; nok=0;                        \
	for i in sh/[0-9][0-9]*; do        \
	  [ -f $$i ] || continue;          \
	  n=$$(expr $$n + 1);              \
	  sh $$i -n 10; 		   \
          if [ $$? -eq 0 ]; then           \
            nok=$$(expr $$nok + 1);     \
            echo "  ## $$i PASSED";     \
          else                          \
            echo "  ## $$i FAILED";     \
          fi;                           \
	done;                           \
	echo "====================================="; \
	echo " Passed $$nok tests of total $$n";      \
	echo "====================================="

