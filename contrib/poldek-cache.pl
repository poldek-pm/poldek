#!/usr/bin/perl -w
#
# Copyright (c) 2002 Michal Moskal <malekith@pld-linux.org>.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgment:
#	This product includes software developed by Michal Moskal.
# 4. Neither the name of the author nor the names of any co-contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY MICHAL MOSKAL AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $Id$
#
# Description:
# 
# This script is kind of poor-man-proxy for poldek. From some point
# of view it is better suited for proxing rpm packages transfers then
# regular proxy servers. For example: it does not check if there is newer
# version of foo-1.2-3.alpha.rpm available, we assume name(a1)==name(a2)
# => contents(a1)==contents(a2) (which makes this script faster then squid
# in case of cache hit), it has heuristic to remove any other version of
# package x from cache if new version is fetched, and so on. However 
# it's still *poor*-man-proxy, so no packages.dir.gz (or any other file 
# name of which doesn't match *.rpm for that matter) is cached, package
# is first fetched from server, and then served to the client (client
# sees no progress indication, until entire package is fetched)
# and so on.
# 
# When it might be useful? 
#   1. if you don't want full squid setup
#   2. you don't want to break your squid to cache files as large as rpm
#      packages
#   3. you install and uninstall the same packages all the time. You can
#      set it up on localhost then.
# 
# My case, and reason to write this stuff, is 3. :-)
# 
# USAGE:
# 
# Install apache. Put this script in 
# /home/services/httpd/cgi-bin/poldek-cache.pl on your.server.net.
# Setup $cache_dir variable, create that directory, do chown http on it.
# Set source = http://your.server.net/cgi-bin/poldek-cache.pl/ra-i686/ or 
# whatever in poldek.conf. Enjoy.
# 
# STATUS:
# 
# Work in progress.
#

# config starts

# where to keep cached files
$cache_dir = "/tmp/poldek-cache/";

# size of cache in megabytes
$cache_size = 500;

# cached directories
%cached = (
	'ra-i386' => 'http://ftp.pld.org.pl/dists/ra/PLD/i386/PLD/RPMS',
	'ra-i586' => 'http://ftp.pld.org.pl/dists/ra/PLD/i586/PLD/RPMS',
	'ra-i686' => 'http://ftp.pld.org.pl/dists/ra/PLD/i686/PLD/RPMS',
	'ra-ppc' => 'http://ftp.pld.org.pl/dists/ra/PLD/ppc/PLD/RPMS',
	'ra-sparc' => 'http://ftp.pld.org.pl/dists/ra/PLD/sparc/PLD/RPMS',
	'ra-alpha' => 'http://ftp.pld.org.pl/dists/ra/PLD/alpha/PLD/RPMS',
	
	'ra-i386-test' => 'http://ftp.pld.org.pl/dists/ra/test/i386',
	'ra-i586-test' => 'http://ftp.pld.org.pl/dists/ra/test/i586',
	'ra-i686-test' => 'http://ftp.pld.org.pl/dists/ra/test/i686',
	'ra-ppc-test' => 'http://ftp.pld.org.pl/dists/ra/test/ppc',
	'ra-sparc-test' => 'http://ftp.pld.org.pl/dists/ra/test/sparc',
	'ra-alpha-test' => 'http://ftp.pld.org.pl/dists/ra/test/alpha',

	'nest-i386' => 'http://ftp.nest.pld.org.pl/PLD/i386/PLD/RPMS',
	'nest-i586' => 'http://ftp.nest.pld.org.pl/PLD/i586/PLD/RPMS',
	'nest-i686' => 'http://ftp.nest.pld.org.pl/PLD/i686/PLD/RPMS',
	'nest-athlon' => 'http://ftp.nest.pld.org.pl/PLD/athlon/PLD/RPMS',
	'nest-ppc' => 'http://ftp.nest.pld.org.pl/PLD/ppc/PLD/RPMS',
	
	'nest-i386-test' => 'http://ftp.nest.pld.org.pl/test/i386',
	'nest-i586-test' => 'http://ftp.nest.pld.org.pl/test/i586',
	'nest-i686-test' => 'http://ftp.nest.pld.org.pl/test/i686',
	'nest-athlon-test' => 'http://ftp.nest.pld.org.pl/test/athlon',
	'nest-ppc-test' => 'http://ftp.nest.pld.org.pl/test/ppc',
);

# end of config

use Fcntl;

$p = $ENV{PATH_INFO};

sub not_found {
  print "Content-type: text/plain\nLocation: /file/not/found\n\n404\n";
  exit 0;
}

sub redirect {
  my $to = shift;
  logmsg("redirect to $to");
  print "Content-type: application/octet-stream\nLocation: $to\n\n";
  exit 0;
}

sub nuke_cache {
  my $current = `du -s $cache_dir`;

  chomp $current;
  $current =~ s/^(\d+).*/$1/;
  
  return unless ($current >= $cache_size * 1024);

  my $need = $current - $cache_size * 1024 * 8 / 10;

  logmsg("cleaning up (${current}k of ${cache_size}m, need to delete ${need}k)");

  open(FH, "find $cache_dir -maxdepth 2 -mindepth 2 -type f -printf '%T@ %k:%p\n' | sort -n |");

  while (<FH>) {
    chomp;
    /^\d+ (\d+):(.*)$/ or die;
    my ($f, $sz) = ($2, $1);
    logmsg("removed $f (${sz}k)");
    unlink($f);
    $need -= $sz;
    last if ($need <= 0);
  }

  while (<FH>) {}
  close(FH);
}

sub pkg_name {
  my $f = shift;
  $f =~ s/.*\/([^\/]+)$/$1/;
  $f =~ s/-[^-]+-[^-]+\.[^.]+\.rpm$//;
  return $f;
}

sub logmsg {
  open(LOG, ">> $cache_dir/log");
  print LOG "$_[0]\n";
  close(LOG);
}

if (defined $p and $p =~ /^\/([a-zA-Z0-9._\-]+)\/([+a-zA-Z0-9._\-\/]+)$/) {
  $vdir = $1;
  $dir = "$cache_dir/$vdir";
  $file = $2;

  not_found unless (defined $cached{$vdir});

  unless (-d $dir) {
    mkdir ("$dir", 0755) or die "can't mkdir $dir ($!)";
  }
  unless (-d "$dir/tmp") {
    mkdir ("$dir/tmp", 0755) or die "can't mkdir $dir/tmp ($!)";
  }

  $url = "$cached{$vdir}/$file";
  
  if (!($file =~ /\//) && ($file =~ /\.rpm$/)) {
    while (not -f "$dir/$file") {
        if (sysopen(HANDLE, "$dir/tmp/$file,lock", O_RDWR|O_CREAT|O_EXCL)) {
	  logmsg("miss: $dir/$file, got lock");
          print HANDLE "$$";
          close HANDLE;
	  # TODO: check if file size get's bigger. if so start transfering it to the client
          $res = system ("wget -q -c $url -O $dir/tmp/$file");
	  if ($res == 0) {
	    # cleanup other versions of this stuff
	    $base = pkg_name($file);
	    while (<$dir/$base*>) {
	      if (pkg_name($_) eq $base) {
	        unlink($_);
		logmsg("clean old version: $_");
	      }
	    }
	    system("mv -f $dir/tmp/$file $dir/$file");
            $now = time;
            utime $now, $now, "$dir/$file";
	    unlink("$dir/tmp/$file,lock");
	    nuke_cache();
	  } else {
	    redirect ($url);
	    exit 0;
	  }
        } else {
	  logmsg("miss, waiting for lock");
          open(HANDLE, "< $dir/tmp/$file,lock") or die;
	  $pid = <HANDLE>;
	  close HANDLE;
	  while (kill(0, $pid)) {
	    sleep(rand());
	    last unless (-f "$dir/tmp/$file,lock");
	  }
	  sleep(rand());
	  logmsg("got lock");
	  if (-f "$dir/tmp/$file,lock") {
	    logmsg("killed old one");
	    unlink("$dir/tmp/$file,lock");
	  }
        }
    }
    
    $size = -s "$dir/$file";
    $now = time;
    utime $now, $now, "$dir/$file";
    logmsg("serving $dir/$file, size = $size");
    print "Content-type: application/octet-stream\nContent-length: $size\n\n";
    system("cat $dir/$file");
    exit 0;
  } else {
    # redirect packages* and stuff
    redirect($url);
  }
} else {
  not_found();
  exit 0;

#  print "Content-type: text/html\n\n";
#  print "<html><body>Following directories are cached:<ul>\n";
#  for (keys %cached) {
#    print "<li>$_ -&gt; <a href=\"$cached{$_}\">$cached{$_}</a></li>\n";
#  }
#  print "</ul></body></html>\n";
#  exit 0;
}

