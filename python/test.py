#!/usr/bin/python
import os
import re
import sys
import string
from types import *
import poldek

ctx = poldek.poldek_ctx()
poldek.cvar.poldek_VERBOSE = 1

src = poldek.source('ac-ready')
ctx.configure(ctx.CONF_SOURCE, src)
ctx.load_config()
ctx.setup()

arr = ctx.get_avail_packages()
print "Loaded %d packages" % len(arr)
n = 0

for ptr in arr:
    print n, ' ', ptr
    n += 1
    
#print "LoadedXX %d packages %d" % (len(arr), n)
    
#    print ptr
#for p in arr:
#    print p

ts = ctx.ts_new(poldek.poldek_ts.INSTALL | poldek.poldek_ts.UPGRADE)
#ts.setop(ts.OP_TEST, 1)
ts.add_pkgmask("python")
ts.add_pkgmask("swig")
ts.run(None)

print "PK"












