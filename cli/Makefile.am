# $Id$

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
AM_CFLAGS = @AM_CFLAGS@

noinst_LTLIBRARIES  = libpoldekcli.la 
libpoldekcli_la_SOURCES  = cli.c cli.h  \
			op.c op.h       \
			op_source.c     \
			op_packages.c   \
			op_makeidx.c    \
			alias.c         \
		        ls.c            \
		        install.c       \
		        uninstall.c     \
		        desc.c          \
		        search.c        \
		        pager.c pager.h

libpoldekcli_la_LIBADD = ../libpoldek.la ../vfile/libvfile.la  
bin_PROGRAMS      = ziuk
ziuk_SOURCES      = main.c
ziuk_LDADD        = libpoldekcli.la   


dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
