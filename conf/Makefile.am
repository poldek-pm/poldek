# $Id$

# generated configs
GENCONFIGS = fetch.conf source.conf poldek.conf

CONFIGS   = $(GENCONFIGS) cli.conf \
            rh-source.conf \
	    fedora-source.conf centos-source.conf

REPO_CONFIGS = pld-source.conf

EXTRA_DIST = $(CONFIGS) \
	     $(REPO_CONFIGS) \
	     sample-source-remoteconf.conf \
             sample-cdrom-source.conf \
             poldek-pri-sample.conf

fetch.conf: $(top_srcdir)/doc/poldek.conf.xml $(top_srcdir)/doc/*.xsl
	$(SHELL) $(top_srcdir)/doc/conf-xml2.sh conf $(top_srcdir)/doc/poldek.conf.xml fetch

source.conf: $(top_srcdir)/doc/poldek.conf.xml $(top_srcdir)/doc/*.xsl
	$(SHELL) $(top_srcdir)/doc/conf-xml2.sh conf $(top_srcdir)/doc/poldek.conf.xml source

poldek.conf: $(top_srcdir)/doc/poldek.conf.xml $(top_srcdir)/doc/*.xsl
	$(SHELL) $(top_srcdir)/doc/conf-xml2.sh conf $(top_srcdir)/doc/poldek.conf.xml poldek

install-data-local: $(GENCONFIGS)
	 $(mkdir_p) $(DESTDIR)$(sysconfdir)/$(PACKAGE)/repos.d
	 for a in $(CONFIGS); do \
	 	$(INSTALL_DATA) $(srcdir)/$$a $(DESTDIR)$(sysconfdir)/$(PACKAGE); \
	 done
	 for a in $(REPO_CONFIGS); do \
		 $(INSTALL_DATA) $(srcdir)/$$a $(DESTDIR)$(sysconfdir)/$(PACKAGE)/repos.d/$$(echo $$a | sed -e 's,-source,,'); \
	 done

uninstall-local:
	 -for a in $(CONFIGS); do \
	 	$(RM) $(DESTDIR)$(sysconfdir)/$(PACKAGE)/$$a; \
	 done
	 -for a in $(REPO_CONFIGS); do \
		$(RM) $(DESTDIR)$(sysconfdir)/$(PACKAGE)/repos.d/$$(echo $$a | sed -e 's,-source,,'); \
	 done

