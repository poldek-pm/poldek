# $Id$

SUBDIRS = pl homepage

POD2HTML  = pod2html

man_MANS  = poldek.1
mandir    = @mandir@/pl


EXTRA_DIST = poldek.pod poldek.1 

%.1: %.pod $@
	$(POD2MAN) -s 1 -r "poldek" --center "poldek @VERSION@" $< > $@

%.html: %.pod
	$(POD2HTML) $< > $@

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\# poldek.html


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 

