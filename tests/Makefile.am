

TESTS = test_poldek
noinst_PROGRAMS = test_poldek
test_poldek_SOURCES = test_poldek.c test_match.c test_env.c \
	   	              test_pmdb.c test_op.c test_config.c test.h

EXTRA_DIST = poldek_test_conf.conf sh


test_poldek_INCLUDES= @CHECK_CFLAGS@
test_poldek_LDADD = @CHECK_LIBS@ ../libpoldek.la ../vfile/libvfile.la

poldek_test_conf.conf: $(top_srcdir)/doc/conf-xml2testconf.xsl $(top_srcdir)/doc/poldek.conf.xml
	xsltproc $(top_srcdir)/doc/conf-xml2testconf.xsl $(top_srcdir)/doc/poldek.conf.xml > poldek_test_conf.conf

clean-local:
	-rm -f *.tmp core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa*
	-find sh -name \*~ | xargs -r rm

tests: poldek_test_conf.conf
	$(MAKE) check-am
	-rm -f poldek_test_conf.*

%.o32: %.S
	nasm -f elf32 $< -o $@

%.o64: %.S
	nasm -f elf64 $< -o $@

%.i686: %.o32
	ld -e _start -o $@ $<
	strip $@
	rm -f $<

%.x86_64: %.o64
	ld -e _start -o $@ $<
	strip $@
	rm -f $<

sh-tests:
	@echo; echo "Running *.sh tests";  \
	find sh -name \*~ | xargs -r rm;   \
	nth=0; nok=0;           	   \
	for i in sh/[0-9][0-9]*; do        \
	  [ -f $$i ] || continue;          \
	  cmpr="gz"; suffixed=""; 	   \
	  grep -q compr-setup $$i;	   \
	  [ $$? -eq 0 ] && cmpr="gz zst none" && suffixed="1"; \
	  for c in $$cmpr; do		   \
	     COMPR="$$c"; export COMPR;    \
	     suffix=""; [ -n "$$suffixed" ] && suffix=" ($$c)"; \
	     nth=$$(expr $$nth + 1);       \
	     sh $$i -n 10; 		   \
	     if [ $$? -eq 0 ]; then        \
	    	nok=$$(expr $$nok + 1);    \
		echo "  ## $$i$$suffix PASSED";    \
	     else                          \
		echo "  ## $$i$$suffix FAILED";    \
	     fi;                           \
	   done;                           \
	done;                              \
	echo "====================================="; \
	echo " Passed $$nok tests of total $$nth";      \
	echo "====================================="
