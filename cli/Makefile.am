# $Id$

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

lib_LTLIBRARIES = libpoclidek.la
libpoclidek_la_SOURCES  =  \
			cli.c cli.h     \
			op.c op.h       \
			cmd_pipe.c cmd_pipe.h \
			cmd.c cmd.h     \
			dent.c dent.h   \
			alias.c         \
			dbcache.c       \
			op_source.c     \
			op_packages.c   \
			op_makeidx.c    \
			op_split.c      \
			op_verify.c     \
		        ls.c            \
		        install.c       \
	        	uninstall.c     \
		        desc.c          \
		        search.c        \
			cd.c            \
			help.c		\
			external.c      \
		        get.c      

libpoclidek_la_LIBADD = ../libpoldek.la ../vfile/libvfile.la  
pkginclude_HEADERS = poclidek.h

bin_PROGRAMS      = poldek
poldek_SOURCES    = shell.c main.c
poldek_LDADD      = libpoclidek.la   

noinst_PROGRAMS   = test_cli
test_cli_SOURCES  = test_cli.c
test_cli_LDADD    = libpoclidek.la

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
