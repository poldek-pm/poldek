# $Id$

SUBDIRS   = vfile

CFLAGS    = @CFLAGS@
INCLUDES  = -I$(top_srcdir)/trurlib
POD2MAN   = @POD2MAN@

noinst_LIBRARIES = libpoldek.a
libpoldek_a_SOURCES = \
	  log.c log.h			\
	  minfo.c			\
	  misc.c misc.h			\
	  rpmadds.c rpmadds.h		\
	  depdirs.c depdirs.h   	\
	  pkg.c pkg.h			\
	  pkgu.c pkgu.h         	\
	  pkgfl.c pkgfl.h		\
	  fileindex.c fileindex.h	\
	  capreqidx.c capreqidx.h	\
	  capreq.c capreq.h		\
	  pkgset.c pkgset.h		\
	  pkgset-def.h			\
	  pkgset-req.c pkgset-req.h	\
	  pkgset-order.c    		\
	  pkgset-load.c pkgset-load.h	\
	  pkgset-install.c		\
	  pkgset-rpmidx.c		\
	  pkgset-txtidx.c		\
	  rpm.c rpm.h 		        \
	  rpmhdr.c rpmhdr.h		\
	  pkgdb.c pkgdb.h		\
	  usrset.c usrset.h		\
	  install.c install.h	    	\
	  conf.c conf.h	    		\
	  shell.c			\
	  h2n.h

bin_PROGRAMS   = poldek
poldek_SOURCES = main.c
poldek_LDADD   = -Ltrurlib -L. -lpoldek -Lvfile -lvfile
INCLUDES       = -Itrurlib

EXTRA_DIST = poldek.1 poldek.pod Makefile.extra *sample* VERSION poldek.spec

man_MANS = poldek.1

poldek.1: poldek.pod
	$(POD2MAN) -s 1 -r "poldek" --center "poldek $(VERSION)" $< > $@

dist-hook:
	rm -rf $(distdir)/.deps

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#*\#



include Makefile.extra