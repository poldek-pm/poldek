#!/usr/bin/perl -w
# $Id$
# add toc to sgml2html output

use strict;

my @toc = ();
my @hdr_lines = ();
my @lines = ();
my $hdr = 1;



while (<>) {
    s/<H1>\s+the\s+poldek//;
    s/<BODY>/<BODY BGCOLOR="#FFFFFF">/; 
    $_ = qq{<a name="logo_info"></a> $_} if /to\s+Karol\s+Krenski/;
    
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

my $tochtml = q{
<div align="center" style="font-size: xx-small">
<img border="0" src="logo.png" alt="poldek's logo (C) Karol Krenski (mimooh at sgsp.edu.pl)">
<a href="#logo_info">*)</a>
</div>
<br><p><ol>};
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

# NFY
sub thanks_to {
    open(F,  "<NEWS") || die;
    while (<F>) {
	if (/(<.+@.+?>)/) {
	    my ($name, $sname, $email) = /(.+?)\s+(.+?)\s+(<.+?@.+?>)/;
	    if ($name && $sname && $email) {
		print STDERR "$name, $sname, $email\n";
	    }
	}
    }
    close(F);
}
