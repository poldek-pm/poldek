# $Id$

SUBDIRS = pl homepage

POD2HTML  = pod2html

man_MANS  = poldek.1

EXTRA_DIST = poldek.pod poldek.1 

manual_index: manual.xml
	./mkindex $< > manual/index.xml

poldek.info: manual.xml $@ manual_index
	docbook2texi $< || true 
	perl -pi -e 's|\@emph\{|\@samp\{|g' poldek_manual.texi
	makeinfo --no-split --force poldek_manual.texi -o $@
	rm -f poldek_manual.texi

%.html: %.xml $@ manual_index
	xmlto xhtml-nochunks $< 
	perl -pi -e 's|<head>|<head><link href="style.css" rel="stylesheet" rev="stylesheet" type="text/css">|' $@

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

