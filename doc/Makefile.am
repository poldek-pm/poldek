NULL =

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
man_MANS   = poldek.1 poldek.conf.1
GEN        = $(man_MANS) manual.html poldek.info
GEN_NODIST = m_index.xml m_config.xml manual.xml.xhtml.tmp manual.xml.texi.tmp

EXTRA_DIST = $(CONFXMLS) $(MANUALS) poldek.1.xml $(GEN) manual CREDITS NEWS.old NEWS.xml NEWS.xsl NEWSdocb.xsl NEWSto mkindex.pl

MANUAL_XHTMLDIR = manual-xhtml

# files referenced in manual.xml (paths relative to manual.xml)
MANUAL_DATAFILES = 						\
	manual/cookbook.xml					\
	manual/ref0700-repository_index_creation.xml		\
	manual/ref0900-repository_selection.xml			\
	manual/ref0901-repository_related_actions.xml		\
	manual/ref1100-package_related_options.xml		\
	manual/ref1290-package_installation.xml			\
	manual/ref1291-distribution_installation-upgrade.xml	\
	manual/ref1300-installation_switches.xml		\
	manual/ref1490-package_deinstallation.xml		\
	manual/ref1700-verification_options-switches.xml	\
	manual/ref1900-splitting_options.xml			\
	manual/ref2100-miscellaneous_options.xml		\
	manual/ref.cmdl.cachedir.xml				\
	manual/ref.cmdl.greedy.xml				\
	manual/ref.cmdl.hold.xml				\
	manual/ref.cmdl.ignore.xml				\
	manual/ref.cmdl.mercy.xml				\
	manual/ref.cmdl.O.xml					\
	manual/ref.cmdl.pmopt.xml				\
	manual/ref.cmdl.promoteepoch.xml			\
	manual/ref.cmdl.test.xml				\
	manual/supported-repositories-table.xml			\
	manual/supported-repositories.xml			\
	../conf/poldek-pri-sample.conf				\
	$(NULL)

doc: $(GEN_NODIST) $(GEN) $(MANUAL_XHTMLDIR) CREDITS

manual-datafiles.stamp:
	@if test $(abs_srcdir) != $(abs_builddir); then \
	    for file in $(MANUAL_DATAFILES); do \
		dir=`dirname $$file`; \
		if test ! -d $$dir; then \
		    $(MKDIR_P) $$dir; \
		fi; \
		cp $(abs_srcdir)/$$file $(abs_builddir)/$$file; \
	    done; \
	fi;
	@touch manual-datafiles.stamp;

m_config.xml: $(CONFXMLS)
	$(SHELL) $(srcdir)/conf-xml2.sh docb $(srcdir)/poldek.conf.xml > $@

m_index.xml: manual.xml m_config.xml $(srcdir)/manual/ref*.xml mkindex.pl
	$(PERL) $(srcdir)/mkindex.pl $(srcdir)/manual.xml m_config.xml $(srcdir)/manual/ref*.xml > $@

manual.xml.xhtml.tmp: manual.xml m_config.xml manual.xsl m_index.xml manual-datafiles.stamp
	m4 -DPOLDEK_VERSION=@VERSION@ $(srcdir)/manual.xml | xmlif output=xhtml > $@

manual.xml.texi.tmp: manual.xml m_index.xml m_config.xml manual.xsl manual-datafiles.stamp
	m4 -DPOLDEK_VERSION=@VERSION@ $(srcdir)/manual.xml | xmlif output=texi > $@

poldek.info: manual.xml.texi.tmp
# NOTE: this "docbook2X2texi" cames from PLD "docbook2X" package
	docbook2X2texi --xinclude $< -o $(@D) || docbook2texi --xinclude $< || true
	perl -pi -e 's|\@emph\{|\@samp\{|g' poldek.texi
	makeinfo --no-split --force poldek.texi -o $@
	perl -pi -e 's/^\* poldek manual: \(poldek\)\. *(.+)$$/* poldek: (poldek).\t\t\t$$1/' $@
	rm -f *.texi

manual.html: manual.xml.xhtml.tmp
	xmlto --skip-validation -m $(srcdir)/manual.xsl -o $(@D) xhtml-nochunks $< && mv manual.xml.xhtml.html $@
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' $@

## NEWS and CREDITS archived with .old 'extension'
#CREDITS.docb.tmp: NEWS.xml NEWSto
#	cd $(srcdir) && $(PERL) ./NEWSto CREDITS.docb > $(abs_builddir)/$@ && cd $(abs_builddir)

#CREDITS: NEWS.xml NEWS.xsl NEWSto CREDITS.docb.tmp
#	xmlto --skip-validation -m $(srcdir)/NEWS.xsl txt CREDITS.docb.tmp && mv CREDITS.docb.txt $@

#CREDITS-body.html: NEWS.xml NEWSto
#	cd $(srcdir) && $(PERL) ./NEWSto CREDITS.html_body > $(abs_builddir)/$@ && cd $(abs_builddir)

#NEWS.docb.tmp: NEWS.xml NEWSdocb.xsl NEWSto
#	cd $(srcdir) && $(PERL) ./NEWSto NEWS.docb > $(abs_builddir)/$@ && cd $(abs_builddir)

#NEWS: NEWS.xml NEWS.xsl NEWSdocb.xsl NEWSto NEWS.docb.tmp
#	xmlto --skip-validation -m $(srcdir)/NEWS.xsl txt NEWS.docb.tmp && mv NEWS.docb.txt $@

#NEWS.html: NEWS.docb.tmp NEWS.xsl
#	xmlto --skip-validation -m $(srcdir)/NEWS.xsl -m $(srcdir)/manual-web.xsl xhtml-nochunks NEWS.docb.tmp && mv NEWS.docb.html $@

$(MANUAL_XHTMLDIR): manual.xml.xhtml.tmp
	$(MKDIR_P) $(MANUAL_XHTMLDIR)
	rm -f $(MANUAL_XHTMLDIR)/*.*
	xmlto --skip-validation -m $(srcdir)/manual.xsl -o $(MANUAL_XHTMLDIR) xhtml $<
# remove empty indexterm's (I'm docbook novice)
	perl -pi -e 's|<dt>[,\s\w\-\=]+<a\shref=""></a></dt>||g' $(MANUAL_XHTMLDIR)/manualindex.html || true
	cp $(srcdir)/manual.css $(MANUAL_XHTMLDIR)

# as above but with manual-web.xsl
manual-web: manual.xml.xhtml.tmp
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
	$(SHELL) $(srcdir)/conf-xml2.sh man $(srcdir)/poldek.conf.xml

poldek.1: poldek.1.xml manual-datafiles.stamp
	if test ! -f $(abs_builddir)/poldek.1.xml; then \
	    cp $(abs_srcdir)/poldek.1.xml $(abs_builddir)/poldek.1.xml; \
	fi; \
	for i in manual/ref[0-9]???-*.xml; do \
	    perl -pe 's/sect2/refsect2/g' < $$i > $$i.tmpman; \
	done; \
	xmlto --skip-validation man poldek.1.xml; \
	rm -f manual/*.tmpman

%.1: %.1.xml
	xmlto --skip-validation man $<

clean-local:
	-rm -r manual-web

	if test $(abs_srcdir) != $(abs_builddir); then \
	    for file in $(MANUAL_DATAFILES); do \
		rm -f $(abs_builddir)/$$file; \
	    done; \
	    rm -f $(abs_builddir)/poldek.1.xml; \
	fi

install-exec-hook:
	$(MKDIR_P) "$(DESTDIR)$(infodir)"
	$(INSTALL_DATA) $(srcdir)/poldek.info $(DESTDIR)$(infodir)

uninstall-hook:
	-$(RM) $(DESTDIR)$(infodir)/poldek.info

distclean-local:
	-rm -r $(MANUAL_XHTMLDIR)

CLEANFILES =				\
	CREDITS				\
	CREDITS-body.html		\
	NEWS				\
	NEWS.html			\
	manual-datafiles.stamp		\
	$(GEN)				\
	$(GEN_NODIST)			\
	$(NULL)

MAINTAINERCLEANFILES =	mkinstalldirs install-sh missing *.tar.gz *.spec \
			config.h.in configure Makefile.in config.h \
			config.sub config.guess aclocal.m4 \
			libtool ltconfig ltmain.sh stamp-h* depcomp *.1 *.texi *.html m_*.xml

.PHONY: reference $(MANUAL_XHTMLDIR)
