#!/usr/bin/python
import os
import re
import string
from types import *
os.environ['PYTHONPATH'] = '.libs'
import poldek

NULL = 'NULL'

def _make_methods(aclass, prefix):
    regexp = re.compile('^%s' % prefix)
    regexp_up = re.compile('^%s' % string.upper(prefix))
    l = len(prefix)
    for k in poldek.__dict__.keys():
        elem = poldek.__dict__[k]
        if regexp.match(k) and type(elem) == BuiltinFunctionType:
            #print "setattr %s %s" % (k[l:], type(elem))
            name = k[l:]
            if not hasattr(aclass, name):
                fn = eval('lambda self, *args: poldek.%s(self, *args)' % k);
                setattr(aclass, name, fn)
        elif regexp_up.match(k):
            name = k[l:]
            if not hasattr(aclass, name):
                setattr(aclass, name, elem)
                #print "%s %s" % (k, type(elem)):
                
            
_make_methods(poldek.poldek_ctx, 'poldek_')
_make_methods(poldek.pkg, 'pkg_')
_make_methods(poldek.tn_array, 'n_array_')

ctx = poldek.poldek_ctx()
poldek.cvar.verbose = 1

path = 'ftp://ftp.pld-linux.org/dists/ra/updates/security/i686/';
src = poldek.source_new('pdir', path, NULL)

#rv = poldek.source_update(src, poldek.PKGSOURCE_UPA)
#print "rv = %d" % rv

ctx.configure(ctx.CONF_SOURCE, src)
print "len0 = %d" % len(ctx.sources)

ctx.load_config()
print "len1 = %d" % len(ctx.sources)
print ctx.setup_sources()
#ctx.configure(ctx.CONF_SOURCE, src)
print "len2 = %d" % len(ctx.sources)

ctx.load_sources()

arr = ctx.get_avpkgs()
print "len = %d" % len(arr)

for i in range(len(arr)):
    p = poldek.pkg(arr[i])
    print p.snprintf_s()
    











