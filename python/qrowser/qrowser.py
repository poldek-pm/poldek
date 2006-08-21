# $Id$
# Package browser app to try and train poldek python module

import sys
import locale
from optparse import OptionParser

import qt
import poldek

from mainWindow import mainWindow

class MainWindow(mainWindow):
    def fillPackageList(self, list):
        for p in list:
            evr = "%s-%s" % (p.ver, p.rel)
            item = qt.QListViewItem(self.PackageListView, p.name, evr, "%ldK" % (p.size / 1024))
            item.__package = p
            
        self.connect( self.PackageListView, qt.SIGNAL("selectionChanged(QListViewItem *)"),
                      self.selectedSlot)

    def selectedSlot(self, item, *args, **kw):
        #print "package ", item.__package
        qt.QApplication.setOverrideCursor(qt.QCursor(qt.QApplication.WaitCursor))
        
        self.showPackageDescription(item.__package)
        self.showPackageCaps(item.__package)
        self.showPackageFiles(item.__package)

        qt.QApplication.restoreOverrideCursor()


    def showPackageFiles(self, pkg):
        self.FileListView.clear()
        
        it = pkg.get_flist_it()
       
        fi = it.get_tuple()
        while fi:
            item = qt.QListViewItem(self.FileListView, fi[0], "%o" % fi[2], "%i" % fi[1])
            fi = it.get_tuple()

    def showPackageCaps(self, pkg):
        self.CapsListView.clear()
        self.ReqsListView.clear()
        self.CnflListView.clear()

        
        caps = pkg.provides()
        if caps:
            for c in caps:
                item = qt.QListViewItem(self.CapsListView, str(c))
        
        caps = pkg.requires()
        if caps:
            for c in caps:
                type = ''
                if c.is_prereq():
                    type += 'pre'

                if c.is_prereq_un():
                    if len(type): type += ', '
                    type += 'preun'

                if c.is_autodirreq():
                    type = 'dir'
                    
                item = qt.QListViewItem(self.ReqsListView, str(c), type)

        caps = pkg.conflicts()
        if caps:
            for c in caps:
                type = 'conflict';
                if c.is_obsl():
                    type = 'obsolete'

                item = qt.QListViewItem(self.CnflListView, str(c), type)
        
        
    def showPackageDescription(self, pkg):
       dsc = "";

       inf = pkg.uinf()
       
       dsc += "<table><tr><td><b>Package: </b></td><td>";
       dsc += str(pkg)
       dsc += "</td></tr><tr><td><b>Summary: </b></td><td>";
       dsc += inf.get(inf.SUMMARY)

       dsc += "</td></tr><tr><td><b>License: </b></td><td>";
       dsc += inf.get(inf.LICENSE)
       
       if inf.get(inf.URL):
           dsc += "</td></tr><tr><td><b>URL: </b></td><td>";
           dsc += '<a href="%s">%s</a>' % (inf.get(inf.URL), inf.get(inf.URL))
           
       dsc += "</td></tr></table>";
       dsc += inf.get(inf.DESCRIPTION)
       self.DescriptionText.setText(dsc);


def get_options():
    parser = OptionParser()
    parser.add_option("-n", "--sn", dest="source")
    (options, args) = parser.parse_args()
    return options


locale.setlocale(locale.LC_ALL, '')
options = get_options()        
if not options.source:
    print "no source specified"
    sys.exit(1)        

poldek.lib_init()

ctx = poldek.poldek_ctx()
#poldek_set_verbose(1)
src = poldek.source(options.source)
ctx.configure(ctx.CONF_SOURCE, src)
ctx.load_config()
if not ctx.setup():
    print "poldek setup failed"
    sys.exit(1)

print "Loading packages..."
arr = ctx.get_avail_packages()
print "Loaded %d packages" % len(arr)
if len(arr) == 0:
    sys.exit(0)

a = qt.QApplication(sys.argv)

w = MainWindow()
w.setGeometry(100, 100, 900, 700)
a.setMainWidget(w)

w.fillPackageList(arr)
w.show()
sys.exit(a.exec_loop())
