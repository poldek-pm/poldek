# $Id$

INCLUDES = @TRURL_INCLUDE@
lib_LIBRARIES      = libvftp.a 
libvftp_a_SOURCES  = vftp.c ftp.c

pkgincludedir = $(includedir)/vftp
pkginclude_HEADERS = vftp.h ftp.h 

noinst_PROGRAMS    = test_vftp
test_vftp_SOURCES  = test_vftp.c
test_vftp_LDADD    = libvftp.a @TRURL_LDFLAG@

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
