#!/usr/bin/python
import os
os.environ['PYTHONPATH'] = '.libs'
from poldek import *
import poldek

ctx = poldek_ctx()

cvar.verbose = 33

poldek_init(ctx, 0)

#src = source()

src = source_new("pdir", 'ftp://ftp.pld-linux.org/dists/ra/updates/security/i686/',
           'ftp://ftp.pld-linux.org/dists/ra/updates/security/i686/')

rv = source_update(src, PKGSOURCE_UPA)
print "rv = %d" % rv

#poldek.poldek_load_config(ctx, '/etc/poldek.conf')
#poldek.poldek_setup_sources(ctx)
#poldek.poldek_load_sources(ctx)

poldek_destroy(ctx)

#print "ctx %s" % ctx










