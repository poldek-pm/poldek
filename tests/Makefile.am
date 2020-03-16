CPPFLAGS = @CPPFLAGS@ @CHECK_CFLAGS@
LDADD = $(top_builddir)/libpoldek.la @CHECK_LIBS@

check_PROGRAMS = test_match test_env test_pmdb test_op test_config \
		 test_store test_cmp

TESTS = $(check_PROGRAMS)

EXTRA_DIST = test.h poldek_test_conf.conf sh run-sh-tests.sh

.PHONY: check-sh
check-sh:
	@./run-sh-tests.sh

# called by test_config
poldek_test_conf.conf: $(top_srcdir)/doc/conf-xml2testconf.xsl $(top_srcdir)/doc/poldek.conf.xml
	xsltproc $(top_srcdir)/doc/conf-xml2testconf.xsl $(top_srcdir)/doc/poldek.conf.xml > poldek_test_conf.conf

clean-local:
	-rm -f *.tmp core *.o *.bak *~ *% *\# TAGS gmon.out \#*\# dupa*
	-find sh -name \*~ | xargs -r rm

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
