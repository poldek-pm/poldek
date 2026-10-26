CPPFLAGS = @CPPFLAGS@ @CHECK_CFLAGS@
LDADD = $(top_builddir)/libpoldek_core.la @CHECK_LIBS@

check_PROGRAMS = test_match test_env test_pmdb test_op test_config \
		 test_store test_cmp test_booldeps test_getenv_path \
		 test_ignore_merge test_log test_pndir test_pkgu test_sigint

TESTS = $(check_PROGRAMS)

# pndir.h includes its siblings by name
test_pndir_CPPFLAGS = -I$(top_srcdir)/pkgdir

EXTRA_DIST = test.h poldek_test_conf.conf sh run-sh-tests.sh

# preloaded by sh/17-interrupt
check_LTLIBRARIES = sigint_at_init.la
sigint_at_init_la_SOURCES = sigint_at_init.c
sigint_at_init_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere

.PHONY: check-sh
check-sh: $(check_LTLIBRARIES)
	@./run-sh-tests.sh

check-sh-no-loop: $(check_LTLIBRARIES)
	@MAX_LOOP=3 ./run-sh-tests.sh

# shared libraries export everything their headers declare EXPORT
TESTS += exports.sh
EXTRA_DIST += exports.sh

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

# a source's flags hold only its options
check_PROGRAMS += test_source_flags

# copying fails cleanly when cp cannot be started
check_PROGRAMS += test_copy_file
