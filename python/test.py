#!/usr/bin/python
import os
import re
import sys
import string
from types import *
import poldek

def test_avail(ctx):
    arr = ctx.get_avail_packages()
    print "Loaded %d packages" % len(arr)
    n = 0
#for ptr in arr:
#    print n, ' ', ptr
#    n += 1


def test_search(ctx):
    arr = ctx.search_avail_packages(ctx.ST_NAME, "poldek")
    print "Found %d package(s)" % len(arr)
    n = 0
    for ptr in arr:
        print n, ' ', ptr
        n += 1

def test_install(ctx):
    ts = ctx.ts_new(poldek.poldek_ts.INSTALL | poldek.poldek_ts.UPGRADE)
    ts.add_pkgmask("python")
    ts.add_pkgmask("swig")
    ts.setop(ts.OP_TEST, True)
    ts.run(None)



def test_cli_ls(cctx):
    cmd = cctx.rcmd_new(None)
    if cmd.execline("desc poldek"):
        pkgs = cmd.get_packages()
        n = 0
        for p in pkgs:
            print n, ' ', p
            n += 1

        if not pkgs:    
            print cmd.get_str()

    

ctx = poldek.poldek_ctx()
poldek.cvar.poldek_VERBOSE = 1
src = poldek.source('ac')
ctx.configure(ctx.CONF_SOURCE, src)
ctx.load_config()
ctx.setup()

cctx = poldek.poclidek_ctx(ctx);
test_cli_ls(cctx)

print "END"












