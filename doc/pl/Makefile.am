# $Id$

man_MANS  = poldek.1
mandir    = @mandir@/pl

EXTRA_DIST = poldek.pod poldek.1 

%.1: %.pod $@
	$(POD2MAN) -s 1 -r "poldek" --center "poldek 0.18.x" $< > $@

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\#


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 
