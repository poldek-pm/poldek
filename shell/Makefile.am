# $Id$

INCLUDES = @TRURL_INCLUDE@
lib_LIBRARIES    = libshell.a 
libshell_a_SOURCES  = shell.c shell.h \
		   term.c  term.h  \
		   ls.c            \
		   install.c       \
		   uninstall.c     \
		   get.c           \
		   desc.c          \
		   search.c 

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
