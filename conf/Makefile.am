# $Id$

CONFIGS   = fetch.conf aliases.conf \
			pld-source.conf rh-source.conf fedora-source.conf

EXTRA_DIST = $(CONFIGS) poldek.conf.in \
             mdk-source.conf sample-cdrom-source.conf \
             poldek-pri-sample.conf 

install-data-local: poldek.conf
	     $(mkdir_p) $(DESTDIR)$(sysconfdir)/$(PACKAGE)
	     $(INSTALL_DATA) poldek.conf $(CONFIGS) $(DESTDIR)$(sysconfdir)/$(PACKAGE)

