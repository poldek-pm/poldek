#!/usr/bin/perl -w
# $Id$
# add toc to sgml2html output

use strict;

my @toc = ();
my @hdr_lines = ();
my @lines = ();
my $hdr = 1;

while (<>) {
    if (m|NAME="(s\d+)">\d+\.\s*(.+)</A>|) {
	$hdr = 0;
	push @toc, {
		    n => $1, 
		    l => $2
		   };
    }

    if ($hdr) {
	push @hdr_lines, $_;
    } else {
	push @lines, $_;
    }	
}

my $tochtml = q{<br><p><ol>};
foreach (@toc) {
    #print "$_->{n} $_->{l}\n";
    $tochtml .= qq{
	<li> <a href="#$_->{n}"> $_->{l} </a> </li> }; 
}
$tochtml .= q{</ol></p>};
foreach (@hdr_lines) {
    print $_;
}
print $tochtml;

foreach (@lines) {
    print $_;
}
