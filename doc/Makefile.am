# $Id$

SUBDIRS = pl homepage manual

# xml configuration description & co.
CONFXMLS  = poldek.conf.xml conf-xml2.sh \
			conf-xml2conf.xsl conf-xml2refentry.xsl conf-xml2docb.xsl

# manual sources 
MANUALS   = manual.xml manual.css 

# generated files 
man_MANS  = poldek.1 poldek.conf.1
GEN       = $(man_MANS) manual.html m_index.xml m_config.xml poldek.info

EXTRA_DIST = $(CONFXMLS) $(MANUALS) poldek.1.xml $(GEN)

doc: $(GEN)

m_index.xml: manual.xml
	./mkindex.pl $< > $@

m_config.xml: $(CONFXMLS)
	./conf-xml2.sh docb poldek.conf.xml > $@

poldek.info: manual.xml m_index.xml m_config.xml
	docbook2texi --xinclude $< || true 
	perl -pi -e 's|\@emph\{|\@samp\{|g' poldek.texi
	makeinfo --no-split --force poldek.texi -o $@
	perl -pi -e 's/^\* poldek manual: \(poldek\)(.+)$$/* poldek: (poldek)$$1/' $@
	rm -f *.texi

manual.html: manual.xml m_index.xml m_config.xml
	xmlto xhtml-nochunks $< 
	perl -pi -e 's|<head>|<head><link href="manual.css" rel="stylesheet" rev="stylesheet" type="text/css">|' $@

poldek.conf.1: $(CONFXMLS)
	./conf-xml2.sh man poldek.conf.xml

%.1: %.1.xml
	xmlto man $<

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\# poldek.html *.tmp


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 *.texi *.html m_*.xml

