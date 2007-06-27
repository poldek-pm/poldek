#!/usr/bin/python
import poldek

def package_inspect(pkg):
    print "Package:  ", pkg

    inf = pkg.uinf()            # loaded on demand
    if inf:                     # index w/o uinf?
        print "== uinf == "
        print "Summary:  ", inf.summary
        print "License:  ", inf.license
        print "URL:      ", inf.url
        print "Vendor:   ", inf.vendor
        print "Buildhost:", inf.buildhost
        print "Distro:   ", inf.distro
        print "Group:    ", pkg.group
        print "Description:\n", inf.description

    print "== properties =="
    if pkg.provides:
        print "Provides:", pkg.provides.join()

    if pkg.conflicts:
        print "Conflicts:", pkg.conflicts.join()

    if pkg.suggests:
        print "Suggests:", pkg.suggests
        
    if pkg.requires:
        print "Requires:", pkg.requires.join()
        for r in pkg.requires:
            type = ''
            if r.is_prereq():
                type += 'pre'

            if r.is_prereq_un():
                if len(type): type += ', '
                type += 'preun'

            if r.is_autodirreq():
                type = 'dir'
                
            if len(type) > 0:
                print "  - req(%s): %s" % (type, r)
            else:
                print "  - req: %s" % r


    print "Files: "
    for (name, size, mode_t) in pkg.files:
        print " ", name


def test_install(ctx):
    ts = ctx.ts_new(poldek.poldek_ts.INSTALL | poldek.poldek_ts.UPGRADE)
    ts.add_pkgmask("python")
    ts.add_pkgmask("swig")
    ts.setop(ts.OP_TEST, True)
    ts.run(None)


def cli_command(cctx, command):
    cmd = cctx.rcmd()
    if cmd.execute(command):
        for p in cmd.packages:
            package_inspect(p)


def init_poldek_ctx(source_name = None):
    ctx = poldek.poldek_ctx()
    #ctx.set_verbose(1)

    src = None
    if source_name: # -n source_name ?
        print "configure %s" % source_name
        src = poldek.source(source_name)
        ctx.configure(ctx.CONF_SOURCE, src)
    ctx.load_config()

    if not ctx.setup():
        raise Exception, "error"

    return (ctx, src)

    

def demo_poldeklib(source_name = None):
    (ctx, src) = init_poldek_ctx(source_name)

    print "Sources: "
    for s in ctx.sources:
        if not src:
            s.set_enabled(False)
        print " -", s

    if src is None and ctx.sources:
        src = ctx.sources[0]
        src.set_enabled(True)    # load first source

    if src is None:
        raise "No sources loaded"
    
    print "Loading %s..." % src
    if ctx.load_sources():
        print "  loaded %d packages" % ctx.packages.length()
        if ctx.packages:
            package_inspect(ctx.packages[0])
        

def demo_poclideklib(source_name = None):
    (ctx, src) = init_poldek_ctx(source_name)

    for s in ctx.sources:
        if not src:
            s.set_enabled(False)
        print " -", s

    if src is None and ctx.sources:
        src = ctx.sources[0]
        src.set_enabled(True)    # load first source

    if src is None:
        raise "No sources loaded"
    
    print "Loading %s..." % src
    cctx = poldek.poclidek_ctx(ctx);
    cctx.load_packages(cctx.LOAD_ALL)   # see poclidek.h
    cli_command(cctx, "ls poldek*");



poldek.lib_init()

demo_poclideklib()
#demo_poldeklib()

