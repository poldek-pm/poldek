#!/usr/bin/perl -w
use strict;
require XML::Simple;
use Data::Dumper;
use vars qw($file $xs $ref $xml);

my %out = ();
foreach my $file (@ARGV) {
    #print STDERR "FILE $file\n";
    $xs = new XML::Simple();

    open(F, "<$file") || die;
    my @lines = <F>;
    close(F);
    if ($lines[0] !~ /\<\?xml\s/) {
        unshift @lines, "<xml>";
        push @lines, "</xml>";
    }
    $ref = $xs->XMLin(join('', @lines), keeproot => 1, keyattr => [], forcecontent => 1);
    traverse($ref->{xml}, \%out);
}

sub traverse {
    my $xml = shift;
    my $outhref = shift || die;
    if (ref $xml eq 'HASH') {
        foreach (keys %$xml) {
            if ($_ eq 'option' || $_ eq 'filename') {
                if (ref $xml->{$_} eq 'HASH') {
                    my $id = $xml->{$_}->{id} || '';
                    my $content = $xml->{$_}->{content};
                    $content =~ s/^\s+//;
                    $content =~ s/\s+$//;
                    print qq{<indexterm zone="$id"><primary>$content</primary></indexterm>\n}
                      if $id;

                    if ($id && $content =~ /\-\-/) {
                        my ($opt) = ($content =~ /\-\-([\w\-]+)/);
                        if ($opt) {
                            print qq{<indexterm zone="$id"><primary>$opt</primary></indexterm>\n};
                        }
                    }
                    #print STDERR "$_ $id, $xml->{$_}->{content}\n";
                    $outhref->{$xml->{$_}->{id}} = $xml->{$_}->{content} if $id;

                } else {
                    traverse($xml->{$_}, $outhref);
                }
            } else {
                traverse($xml->{$_}, $outhref);
            }
        }

    } elsif (ref $xml eq 'ARRAY') {
        foreach my $elem (@{$xml}) {
            traverse($elem, $outhref);
        }
    }
}
