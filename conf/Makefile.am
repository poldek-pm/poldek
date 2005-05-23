# $Id$

# generated configs
GENCONFIGS = fetch.conf source.conf poldek.conf

CONFIGS   = $(GENCONFIGS) aliases.conf \
			pld-source.conf rh-source.conf fedora-source.conf

EXTRA_DIST = $(CONFIGS) \
			 sample-source-remoteconf.conf \
             sample-cdrom-source.conf \
             poldek-pri-sample.conf

%.conf: ../doc/poldek.conf.xml ../doc/*.xsl
	../doc/conf-xml2.sh conf ../doc/poldek.conf.xml $*

install-data-local: $(GENCONFIGS)
	     $(mkdir_p) $(DESTDIR)$(sysconfdir)/$(PACKAGE)
	     $(INSTALL_DATA) $(CONFIGS) $(DESTDIR)$(sysconfdir)/$(PACKAGE)

