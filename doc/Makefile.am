# $Id$

# NOTE
# $(GEN) files are automatically generated, don't modify them. See
# following rules to see from what particular file is generated from.

SUBDIRS = pl homepage

# xml configuration description & co.
CONFXMLS  = poldek.conf.xml conf-xml2.sh \
			conf-xml2conf.xsl conf-xml2refentry.xsl conf-xml2docb.xsl

# manual sources 
MANUALS   = manual.xml manual.css manual.xsl

# generated files 
man_MANS  = poldek.1 poldek.conf.1
GEN       = $(man_MANS) manual.html m_index.xml m_config.xml poldek.info

EXTRA_DIST = $(CONFXMLS) $(MANUALS) poldek.1.xml $(GEN) manual 

MANUAL_XHTMLDIR = manual-xhtml

doc: $(GEN) $(MANUAL_XHTMLDIR)

m_index.xml: manual.xml m_config.xml manual/ref*.xml mkindex.pl
	./mkindex.pl manual.xml m_config.xml manual/ref*.xml > $@

m_config.xml: $(CONFXMLS)
	./conf-xml2.sh docb poldek.conf.xml > $@

manual.xml.xhtml.tmp: manual.xml m_config.xml manual.xsl m_index.xml
	m4 -DPOLDEK_VERSION=@VERSION@ manual.xml | xmlif output=xhtml > $@

manual.xml.texi.tmp: manual.xml m_index.xml m_config.xml manual.xsl
	m4 -DPOLDEK_VERSION=@VERSION@ manual.xml | xmlif output=texi > $@

poldek.info: manual.xml.texi.tmp
	docbook2texi --xinclude $< || true 
	perl -pi -e 's|\@emph\{|\@samp\{|g' poldek.texi
	makeinfo --no-split --force poldek.texi -o $@
	perl -pi -e 's/^\* poldek manual: \(poldek\)(.+)$$/* poldek: (poldek)$$1/' $@
	rm -f *.texi

manual.html: manual.xml.xhtml.tmp
	xmlto -m manual.xsl xhtml-nochunks $< && mv manual.xml.xhtml.html $@
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' $@


$(MANUAL_XHTMLDIR): manual.xml.xhtml.tmp
	$(mkdir_p) $(MANUAL_XHTMLDIR)
	rm -f $(MANUAL_XHTMLDIR)/*.*
	xmlto -m manual.xsl -o $(MANUAL_XHTMLDIR) xhtml $<
# add the stylesheet and removed empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' $(MANUAL_XHTMLDIR)/ix??.html || true
	cp manual.css $(MANUAL_XHTMLDIR)

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
    xmlto man $<;                                         \
    rm -f manual/*.tmpman

%.1: %.1.xml
	xmlto man $<

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
