# $Id$

INCLUDES = @TRURL_INCLUDE@
lib_LIBRARIES    = shell.a 
shell_a_SOURCES  = shell.c shell.h \
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
