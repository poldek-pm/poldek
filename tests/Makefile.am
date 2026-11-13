CPPFLAGS = @CPPFLAGS@ @CHECK_CFLAGS@
LDADD = $(top_builddir)/libpoldek_core.la @CHECK_LIBS@

check_PROGRAMS = test_match test_env test_pmdb test_op test_config \
		 test_store test_cmp test_booldeps test_getenv_path \
		 test_ignore_merge test_log test_pndir test_pkgu test_sigint

# a source's flags hold only its options
check_PROGRAMS += test_source_flags
# copying fails cleanly when cp cannot be started
check_PROGRAMS += test_copy_file
# a ^C while a command runs, and what poldek passes on to it
check_PROGRAMS += test_relay_output
# build day sorting, which lives in the CLI library
check_PROGRAMS += test_dent

TESTS = $(check_PROGRAMS)

# MALLOC_PERTURB_ overwrites freed memory, but not a block glibc keeps in its
# thread cache; with the cache off a test that reads freed memory gets garbage
AM_TESTS_ENVIRONMENT = GLIBC_TUNABLES=glibc.malloc.tcache_count=0; \
	export GLIBC_TUNABLES; MALLOC_PERTURB_=165; export MALLOC_PERTURB_;

# pndir.h includes its siblings by name
test_pndir_CPPFLAGS = -I$(top_srcdir)/pkgdir

# pkg_dent_cmp_bday() is part of the CLI library
test_dent_LDADD = $(top_builddir)/cli/libpoclidek.la @CHECK_LIBS@

EXTRA_DIST = test.h poldek_test_conf.conf \
	sh run-sh-tests.sh metadata \
	build-test-rpm.sh

# preloaded by sh/16-hooks, sh/17-interrupt and sh/19-questions
check_LTLIBRARIES = sigint_at_init.la signal_at_fork.la signal_at_restore.la \
		    library_user.la front_end.la
sigint_at_init_la_SOURCES = sigint_at_init.c
sigint_at_init_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
signal_at_fork_la_SOURCES = signal_at_fork.c
signal_at_fork_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
signal_at_restore_la_SOURCES = signal_at_restore.c
signal_at_restore_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
library_user_la_SOURCES = library_user.c
library_user_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
front_end_la_SOURCES = front_end.c
front_end_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere

.PHONY: check-sh
check-sh: $(check_LTLIBRARIES)
	@./run-sh-tests.sh

check-sh-no-loop: $(check_LTLIBRARIES)
	@MAXLOOP=3 ./run-sh-tests.sh

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
