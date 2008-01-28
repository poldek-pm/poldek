#!/usr/bin/python
# $Id$
#
# pyldek - Python poldek API consistency verification 

import sys
import string
from optparse import OptionParser
try:
    import poldek
except ImportError, e:
    print e
    #print "run me this way: ./run.sh %s" % sys.argv[0]
    raise ImportError, "run me via $ ./run.sh %s" % sys.argv[0]
    

class vfileProgress(poldek.vfile_progress):
    def __init__(self):
        self.label = None

    def initializex(self, label):
        print "[py] downloading %s..." % label
        self.label = label

    def reset(self):
        pass
    
    def progress(self, total, amount):
        if amount == 0:
            print " start downloading"
        elif amount == -1:
            print "\n end downloading"
        else:
            print "\r  - got %d of total %d" % (amount, total),


class PyldekCallbacks(poldek.callbacks):
    def __init__(self, prefix = 'py'):
        self.prefix = prefix

    def log(self, pri, message):
        if pri is None or len(pri) == 0:
            pri = 'info'
            
        if pri == 'cont':
            sys.stdout.write(message)
        else:
            lines = string.split(message, '\n')
            if len(lines[-1]) == 0:
                del lines[-1]
                
            if len(lines) == 1:
                print "[%s %s] %s" % (self.prefix, pri, message),
            else:
                for l in lines:
                    print "[%s %s] %s" % (self.prefix, pri, l)

        sys.stdout.flush()

    def confirm(self, ts, hint, message):
        shint = "YES"
        if not hint: shint = "NO"
        
        print "\n****************  Please confirm ***************************"
        print message, "(yes/no)? Hint is <%s>" % shint
        print "************************************************************\n"
        return hint

    def print_summary(self, mark, packages):
        for p in packages:
            print "%s %s" % (mark, p)


    def confirm_transaction(self, ts):
        print "\n************** transaction confirmation *********************"
        
        if ts.type == ts.UNINSTALL:
            print "* I'm going to remove following packages:"
        else:
            print "* Following changes will be made:"
        
        for m in [ 'I', 'D', 'R' ]:
            self.print_summary("* %s" % m, ts.summary(m))
        print "* "    
        print "* Do you really want to proceed?"
        print "**************************************************************"
        print " - NOPE"
        return False
    
    def choose_equiv(self, ts, capability_name, packages, hint):
        print "\n************** choose package *********************"
        print "* Following packages provide <%s>" % capability_name
        print "* Choose one (hint is %s):" % hint
        n = 0
        for p in packages:
            print "*  %d. %s" % (n, p)
            n += 1
        print "*****************************************************\n"
        return hint


class Pyldek:
    def __init__(self, source_name = None, verbose = 1, config = None):
        ctx = poldek.poldek_ctx()
        self._cb = PyldekCallbacks()
        self._progress = vfileProgress()
        
        ctx.set_callbacks(self._cb)
        ctx.set_vfile_progress(self._progress)
        ctx.set_verbose(verbose)

        src = None
        if source_name: # -n source_name ?
            print "## Configured %s" % source_name
            src = poldek.source(source_name)
            ctx.configure(ctx.CONF_SOURCE, src)

        ctx.load_config()
    
        if not ctx.setup():
            raise RuntimeError, "poldek setup failed"

        self.ctx = ctx
        self.cctx = poldek.poclidek_ctx(ctx)

    def load_packages(self):
        self.cctx.load_packages(self.cctx.LOAD_ALL)   # see poclidek.h
        
    def repository_list(self):
        self.load_packages()            # make sure repos is loaded
        repos = self.ctx.pkgdirs
        print "## Loaded repositories"
        for r in repos:
            print "  - %s, type=%s,path=%s" % (r, r.type, r.path)


    def print_summary(self, mark, packages):
        for p in packages:
            print "%s %s" % (mark, p)

    def install(self, packages = [], masks = []):
        for p in packages:
            masks.append(p.__str__())
        command = "install " + string.join(masks, ' ')
        
        ts = self.ctx.ts_new()
        cmd = self.cctx.rcmd(ts)
        if cmd.execute(command):
            return True
            #for m in [ 'I', 'D', 'R' ]:
            #    self.print_summary(m, ts.summary(m))
        return False    

    def uninstall(self, packages = [], masks = []):
        for p in packages:
            masks.append(p.__str__())
        command = "uninstall  " +  + string.join(masks, ' ')
        
        ts = self.ctx.ts_new()
        cmd = self.cctx.rcmd(ts)
        if cmd.execute(command):
            return True
            #for m in [ 'R', 'D' ]:
            #    self.print_summary(m, ts.summary(m))
        return False


    def cli_directory_list(self):      # directories
        self.load_packages()            # make sure repos is loaded
        pwd = self.cctx.pwd()
        repos = None
        cmd = self.cctx.rcmd()
        if cmd.execute("cd /; ls"):
            repos = string.split(cmd.output, '\n')
            if len(repos) > 1:
                del repos[-1]
        self.cctx.chdir(pwd) 
        return repos

    def list_sources(self):
        if self.ctx.sources:
           print "%-12s %-8s  %s" % ('Name', 'Type', 'Path')
           for src in self.ctx.sources:
               print "%-12s %-8s  %s" % (src, src.type, src.path)

    def update_sources(self, upa = False):
        flag = poldek.source.UP
        if upa:
            flag = poldek.source.UPA
        for src in self.ctx.sources:
            print "Updating %s" % src
            src.update(flag)


    def execute_and_return_packages(self, command, args = ''):
        cmd = self.cctx.rcmd()
        print "## Executing %s %s" % (command, args)
        if cmd.execute("%s %s" % (command, args)):
            return cmd.packages

    def execute_pkg_command(self, command, args = ''):
        cmd = self.cctx.rcmd()
        print "## Executing %s %s" % (command, args)
        if cmd.execute("%s %s" % (command, args)):
            for p in cmd.packages:
                print "  -", p
                
    def execute_str_command(self, command, args = ''):
        cmd = self.cctx.rcmd()
        print "## Executing %s %s" % (command, args)
        if cmd.execute("%s %s" % (command, args)):
            print cmd.output
        
    def desc(self, pkg):
        print "### Package:  ", pkg

        inf = pkg.uinf()            # user level info is loaded on demand
        if inf:                     # index with uinf?
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

        if pkg.files:
            print "Files: "
            for (name, size, mode_t) in pkg.files:
                print " ", name

def get_options():
    usage = "pyldek [OPTIONS] -- [COMMAND COMMAND_ARGS] "
    parser = OptionParser(usage=usage)
    parser.add_option("-l", action='count', help="List sources")
    parser.add_option("-v",  action='count', help="Be verbose")
    parser.add_option("-n", metavar="source", help="Select repository")
    parser.add_option("--up", action="count", help="Update repository/ies")
    parser.add_option("--upa", action="count", help="Update repository/ies")
    (options, args) = parser.parse_args()
    if not options.l and not options.n and len(args) < 1:
        parser.print_help()
        sys.exit(0)
    return (options, args)

(options, args) = get_options()

poldek.lib_init()
if not options.v :
    options.v = 0
    
pyl = Pyldek(options.n, verbose = options.v)

if options.l:
    pyl.list_sources()
    
elif options.up:
    pyl.update_sources()
    
elif options.upa:
    pyl.update_sources(True)
    
else:
    #pyl.repository_list()

    if options.v == 0:
        args.append("-q")     # switch off messages, progress bars, etc
    a_command = string.join(args, ' ')
    command = args[0]

    if command in [ 'ls', 'search', 'llu' ]:
        pyl.execute_pkg_command(a_command)

    elif command in [ 'install' ]:
        pyl.install(masks = args[1:])
        
    elif command == 'desc':
        packages = pyl.execute_and_return_packages('ls ' + string.join(args[1:], ' '))
        if packages:
            for p in packages:
                pyl.desc(p)
    else:
        pyl.execute_str_command(a_command)


