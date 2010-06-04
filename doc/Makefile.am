# $Id$

# NOTE
# $(GEN) files are automatically generated, don't modify them. See
# following rules to see from what particular file is generated from.

SUBDIRS = pl homepage

# xml configuration description & co.
CONFXMLS  = poldek.conf.xml conf-xml2.sh \
			conf-xml2c.xsl conf-xml2conf.xsl conf-xml2refentry.xsl conf-xml2docb.xsl

# manual sources
MANUALS   = manual.xml manual.css manual.xsl

# generated files
man_MANS  = poldek.1 poldek.conf.1
GEN       = $(man_MANS) manual.html m_index.xml m_config.xml poldek.info

EXTRA_DIST = $(CONFXMLS) $(MANUALS) poldek.1.xml $(GEN) manual CREDITS

MANUAL_XHTMLDIR = manual-xhtml

doc: $(GEN) $(MANUAL_XHTMLDIR) CREDITS

m_index.xml: manual.xml m_config.xml manual/ref*.xml mkindex.pl
	./mkindex.pl manual.xml m_config.xml manual/ref*.xml > $@

m_config.xml: $(CONFXMLS)
	./conf-xml2.sh docb poldek.conf.xml > $@

manual.xml.xhtml.tmp: manual.xml m_config.xml manual.xsl m_index.xml
	m4 -DPOLDEK_VERSION=@VERSION@ manual.xml | xmlif output=xhtml > $@

manual.xml.texi.tmp: manual.xml m_index.xml m_config.xml manual.xsl
	m4 -DPOLDEK_VERSION=@VERSION@ manual.xml | xmlif output=texi > $@

poldek.info: manual.xml.texi.tmp
# NOTE: this "docbook2X2texi" cames from PLD "docbook2X" package
	docbook2texi --xinclude $< || docbook2X2texi --xinclude $< || true
	perl -pi -e 's|\@emph\{|\@samp\{|g' poldek.texi
	makeinfo --no-split --force poldek.texi -o $@
	perl -pi -e 's/^\* poldek manual: \(poldek\)(.+)$$/* poldek: (poldek)$$1/' $@
	rm -f *.texi

manual.html: manual.xml.xhtml.tmp
	xmlto --skip-validation -m manual.xsl xhtml-nochunks $< && mv manual.xml.xhtml.html $@
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' $@

CREDITS.docb.tmp: NEWS.xml NEWSto
	./NEWSto CREDITS.docb > $@

CREDITS: CREDITS.docb.tmp
	 xmlto --skip-validation -m NEWS.xsl txt CREDITS.docb.tmp && mv CREDITS.docb.txt $@

CREDITS-body.html: NEWS.xml NEWSto
	 ./NEWSto CREDITS.html_body > $@

NEWS.docb.tmp: NEWS.xml NEWSdocb.xsl NEWSto
	./NEWSto NEWS.docb > $@

NEWS: NEWS.docb.tmp NEWS.xsl
	xmlto --skip-validation -m NEWS.xsl txt NEWS.docb.tmp && mv NEWS.docb.txt NEWS

NEWS.html: NEWS.docb.tmp NEWS.xsl
	xmlto --skip-validation -m NEWS.xsl -m manual-web.xsl xhtml-nochunks NEWS.docb.tmp && mv NEWS.docb.html NEWS.html

$(MANUAL_XHTMLDIR): manual.xml.xhtml.tmp
	$(mkdir_p) $(MANUAL_XHTMLDIR)
	rm -f $(MANUAL_XHTMLDIR)/*.*
	xmlto --skip-validation -m manual.xsl -o $(MANUAL_XHTMLDIR) xhtml $<
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' $(MANUAL_XHTMLDIR)/manualindex.html || true
	cp manual.css $(MANUAL_XHTMLDIR)

# as above but with manual-web.xsl
manual-web: manual.xml.xhtml.tmp
	$(mkdir_p) manual-web
	rm -f manual-web/*.*
	xmlto --skip-validation -m manual.xsl -m manual-web.xsl -o manual-web xhtml $<
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' manual-web/manualindex.html || true
	cp manual.css manual-web


reference:
	touch ../cli/main.c
	../make.sh  CFLAGS+="-DGENDOCBOOK=1"
	../cli/poldek --docb

poldek.conf.1: $(CONFXMLS)
	./conf-xml2.sh man poldek.conf.xml

poldek.1: poldek.1.xml
	for i in manual/ref[0-9]???-*.xml; do                 \
        perl -pe 's/sect2/refsect2/g' < $$i > $$i.tmpman; \
    done;                                                 \
    xmlto --skip-validation man $<;                                         \
    rm -f manual/*.tmpman

%.1: %.1.xml
	xmlto --skip-validation man $<

install-exec-hook:
	$(mkdir_p) "$(DESTDIR)$(infodir)"
	$(INSTALL_DATA) poldek.info $(DESTDIR)$(infodir)

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\# poldek.html *.tmp

distclean-local:
	-rm -r $(MANUAL_XHTMLDIR)


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 *.texi *.html m_*.xml

.PHONY: reference $(MANUAL_XHTMLDIR)
