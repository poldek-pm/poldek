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

EXTRA_DIST = $(CONFXMLS) $(MANUALS) poldek.1.xml $(GEN) manual CREDITS NEWS.xml NEWS.xsl NEWSdocb.xsl NEWSto mkindex.pl

MANUAL_XHTMLDIR = manual-xhtml

doc: $(GEN) $(MANUAL_XHTMLDIR) CREDITS

m_index.xml: manual.xml m_config.xml $(srcdir)/manual/ref*.xml mkindex.pl
	$(PERL) $(srcdir)/mkindex.pl $(srcdir)/manual.xml m_config.xml $(srcdir)/manual/ref*.xml > $@

$(abs_builddir)/m_config.xml: $(CONFXMLS)
	cd $(srcdir) && $(SHELL) conf-xml2.sh docb poldek.conf.xml > $@ && cd $(@D)

$(abs_builddir)/manual.xml.xhtml.tmp: manual.xml m_config.xml manual.xsl m_index.xml
	cd $(srcdir) && m4 -DPOLDEK_VERSION=@VERSION@ manual.xml | xmlif output=xhtml > $@ && cd $(@D)

$(abs_builddir)/manual.xml.texi.tmp: manual.xml m_index.xml m_config.xml manual.xsl
	cd $(srcdir) && m4 -DPOLDEK_VERSION=@VERSION@ manual.xml | xmlif output=texi > $@ && cd $(@D)

$(abs_builddir)/poldek.info: $(abs_builddir)/manual.xml.texi.tmp
# NOTE: this "docbook2X2texi" cames from PLD "docbook2X" package
	cd $(srcdir) && docbook2X2texi --xinclude $< -o $(@D) || docbook2texi --xinclude $< || true && cd $(@D)
	perl -pi -e 's|\@emph\{|\@samp\{|g' poldek.texi
	makeinfo --no-split --force poldek.texi -o $@
	perl -pi -e 's/^\* poldek manual: \(poldek\)(.+)$$/* poldek: (poldek)$$1/' $@
	rm -f *.texi

$(abs_builddir)/manual.html: $(abs_builddir)/manual.xml.xhtml.tmp
	cd $(srcdir) && xmlto --skip-validation -m manual.xsl -o $(@D) xhtml-nochunks $< && mv manual.xml.xhtml.html $@ && cd $(@D)
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' $@

$(abs_builddir)/CREDITS.docb.tmp: NEWS.xml NEWSto
	cd $(srcdir) && $(PERL) ./NEWSto CREDITS.docb > $@ && cd $(@D)

CREDITS: NEWS.xml NEWS.xsl NEWSto
	$(MAKE) $(AM_MAKEFLAGS) $(abs_builddir)/CREDITS.docb.tmp
	xmlto --skip-validation -m $(srcdir)/NEWS.xsl txt CREDITS.docb.tmp && mv CREDITS.docb.txt $@

$(abs_builddir)/CREDITS-body.html: NEWS.xml NEWSto
	cd $(srcdir) && $(PERL) ./NEWSto CREDITS.html_body > $@ && cd $(@D)

$(abs_builddir)/NEWS.docb.tmp: NEWS.xml NEWSdocb.xsl NEWSto
	cd $(srcdir) && $(PERL) ./NEWSto NEWS.docb > $@ && cd $(@D)

NEWS: NEWS.xml NEWS.xsl NEWSdocb.xsl NEWSto
	$(MAKE) $(AM_MAKEFLAGS) $(abs_builddir)/NEWS.docb.tmp
	xmlto --skip-validation -m $(srcdir)/NEWS.xsl txt NEWS.docb.tmp && mv NEWS.docb.txt $@

NEWS.html: $(abs_builddir)/NEWS.docb.tmp NEWS.xsl
	xmlto --skip-validation -m $(srcdir)/NEWS.xsl -m $(srcdir)/manual-web.xsl xhtml-nochunks NEWS.docb.tmp && mv NEWS.docb.html $@

$(MANUAL_XHTMLDIR): $(abs_builddir)/manual.xml.xhtml.tmp
	$(MKDIR_P) $(MANUAL_XHTMLDIR)
	rm -f $(MANUAL_XHTMLDIR)/*.*
	xmlto --skip-validation -m $(srcdir)/manual.xsl -o $(MANUAL_XHTMLDIR) xhtml $<
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' $(MANUAL_XHTMLDIR)/manualindex.html || true
	cp $(srcdir)/manual.css $(MANUAL_XHTMLDIR)

# as above but with manual-web.xsl
manual-web: $(abs_builddir)/manual.xml.xhtml.tmp
	$(MKDIR_P) manual-web
	rm -f manual-web/*.*
	xmlto --skip-validation -m $(srcdir)/manual.xsl -m $(srcdir)/manual-web.xsl -o manual-web xhtml $<
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' manual-web/manualindex.html || true
	cp $(srcdir)/manual.css manual-web


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

clean-local:
	-rm -f core *.o *.bak *~ *% *\# TAGS gmon.out \#* *\# poldek.html *.tmp

install-exec-hook:
	$(MKDIR_P) "$(DESTDIR)$(infodir)"
	$(INSTALL_DATA) $(srcdir)/poldek.info $(DESTDIR)$(infodir)

uninstall-hook:
	-$(RM) $(DESTDIR)$(infodir)/poldek.info

distclean-local:
	-rm -r $(MANUAL_XHTMLDIR)


MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 *.texi *.html m_*.xml

.PHONY: reference $(MANUAL_XHTMLDIR)
