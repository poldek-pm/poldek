#!/usr/bin/perl -w
use strict;
require XML::Simple;
use Data::Dumper;
use vars qw($file $xs $ref $xml $current_id);

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
    #print Dumper($ref);
    traverse(undef, $ref, \%out);
}

foreach my $term (keys %out) {
    my %h = map { $_ => 1 } @{$out{$term}};
    my $id = join(' ', keys %h);
    print qq{<indexterm zone="$id"><primary>$term</primary></indexterm>\n};
}

sub traverse {
    my $key = shift || 'undef';
    my $xml = shift;
    my $outhref = shift || die;

    #print STDERR "traverse $key, $xml\n" if $key eq 'filename';
    if (ref $xml eq 'HASH') {
        $current_id = '' if exists $xml->{sect1} ||
          exists $xml->{sect2} || exists $xml->{sect3};

        my $previous = $current_id;
        $current_id = $xml->{id} if $xml->{id} && exists $xml->{title};
        $previous ||= '(empty)';
        my $current_id_str = $current_id || '(empty)';
        #print STDERR "ID $previous -> $current_id_str\n"
        #  if $previous ne $current_id_str;
    }

    if (ref $xml eq 'ARRAY') {
        foreach my $elem (@{$xml}) {
            next if !ref $elem && $key eq 'filename'; # index <file>s with ids only
            next if !$key eq 'filename' && exists $elem->{id};
            traverse($key, $elem, $outhref);
        }

    } elsif (ref $xml eq 'HASH') {
        foreach (keys %$xml) {
            my $akey = $_;
            if ($_ eq 'content') {
                #index all <option>s
                $akey = $key if $key eq 'option';
                # and <file>s with ids
                if ($key eq 'filename' && exists $xml->{id}) {
                    $akey = $key;
                    #print STDERR " DO $akey $xml->{id} $xml->{content}\n";
                }
            }
            traverse($akey, $xml->{$_}, $outhref);
        }
    }

    return if ref $xml;
    return if $key ne 'option' && $key ne 'filename';

    my $content = $xml;
    $content =~ s/^([^\=]+)/$1/ if $content =~ /=/;
    $content =~ s/^\s+//;
    $content =~ s/\s+$//;
    $content .= " file" if $key eq 'filename';
    $outhref->{$content} ||= [];

    push @{$outhref->{$content}}, $current_id;
    #print STDERR "  do $content $current_id\n";

    return;
    if ($content =~ /\-\-/) {
        my ($opt) = ($content =~ /\-\-([\w\-]+)/);
        if ($opt) {
            $outhref->{$opt} ||= [];
            push @{$outhref->{$opt}}, $current_id;
        }
    }
}
