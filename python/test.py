#!/usr/bin/python
import os
import re
import string
from types import *
os.environ['PYTHONPATH'] = '.libs'
import poldek

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
_make_methods(poldek.poldek_ts, 'poldek_ts_')
_make_methods(poldek.pkg, 'pkg_')
_make_methods(poldek.source, 'source_')
_make_methods(poldek.tn_array, 'n_array_')

ctx = poldek.poldek_ctx()
poldek.cvar.poldek_VERBOSE = 1

src = poldek.source('ac-ready')
ctx.configure(ctx.CONF_SOURCE, src)
ctx.load_config()
ctx.setup()

arr = ctx.get_avail_packages()
print "Loaded %d packages" % len(arr)
n = 0
#for ptr in arr:
#    print ptr
#print "LoadedXX %d packages %d" % (len(arr), n)
    
#    print ptr
#for p in arr:
#    print p

ts = ctx.ts_new()
ts.set_type(ts.INSTALL, "from python")
ts.setf(ts.UPGRADE)
ts.setop(poldek.POLDEK_OP_TEST, 1)
ts.add_pkgmask("poldek")
ts.run(None)













