NULL =

pkgincludedir = $(includedir)/poldek/poclidek

AM_CPPFLAGS = -I$(top_srcdir) @TRURL_INCLUDE@
AM_CFLAGS = @AM_CFLAGS@

SHELL_MOD = shell.c

libVERINFO         = 2:0:0
lib_LTLIBRARIES    = libpoclidek.la

libpoclidek_la_LDFLAGS = -version-info $(libVERINFO)

libpoclidek_la_SOURCES  =  \
			cli.c cli.h     \
			op.c op.h     \
			cmd_pipe.c cmd_pipe.h  \
			cmd_chain.c cmd_chain.h     \
			dent.c dent.h \
			alias.c         \
			dbcache.c       \
			op_source.c     \
			op_packages.c   \
			op_makeidx.c    \
			op_verify.c     \
		        ls.c            \
		        ls_queryfmt.c ls_queryfmt.h \
		        install.c       \
			uninstall.c     \
	        	desc.c          \
		    	search.c        \
			reload.c	\
			pull.c		\
			clean.c		\
			cd.c            \
			help.c		\
			external.c      \
		    	get.c           \
			cmd.h           \
			rcmd.c

libpoclidek_la_LIBADD = ../libpoldek.la ../vfile/libvfile.la

libHEADERS = poclidek.h cmd.h dent.h op.h cli.h
pkginclude_HEADERS = $(libHEADERS)

if ENABLE_INTERACTIVE_MODE
SHELL_MOD_ = $(SHELL_MOD)
else
SHELL_MOD_ =
endif


bin_PROGRAMS      = poldek
poldek_SOURCES    = $(SHELL_MOD_) main.c su.c
poldek_LDADD      = libpoclidek.la

noinst_PROGRAMS   = test_cli poclidek_demo
test_cli_SOURCES  = test_cli.c
test_cli_LDADD    = libpoclidek.la

poclidek_demo_SOURCES = poclidek_demo.c
poclidek_demo_LDADD = libpoclidek.la

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1
