# $Id$

INCLUDES = @TRURL_INCLUDE@ -I$(top_srcdir)
lib_LIBRARIES    = libshell.a 
libshell_a_SOURCES  = shell.c shell.h \
		   ls.c            \
		   install.c       \
		   uninstall.c     \
		   get.c           \
		   desc.c          \
		   search.c        \
		   pager.c pager.h

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
