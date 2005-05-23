#!/bin/sh

DN=$(dirname $0)

to_man() {
    tmp=$FILE.tmp
    # - replace [screen] to [programlisting], docbook2man do better rendering for it
    # - replace [foo] and [/foo] with &lt;foo&gt;
    # - replace [ foo ] with [foo]
    perl -pe 's/\[(.?)screen\]/[$1programlisting]/g; s/\[(\w)/&lt;$1/g; s|\[/|&lt;/|g; s/(\w)\]/$1&gt;/g; s/\[\s(\w+)\s\]/[$1]/g;' $FILE > $tmp

    xsltproc $DN/conf-xml2refentry.xsl $tmp | perl -ne 's|&lt;!|<!|; s|&gt;|>|;print' > $tmp.ref.tmp && xmlto man $tmp.ref.tmp
}

to_docb() {
    tmp=$FILE.tmp
    # - replace [foo] and [/foo] with &lt;foo&gt;
    # - replace [ foo ] with [foo]
    perl -pe 's/\[(\w)/&lt;$1/g; s|\[/|&lt;/|g; s/(\w)\]/$1&gt;/g; s/\[\s(\w+)\s\]/[$1]/g;' $FILE > $tmp

    xsltproc $DN/conf-xml2docb.xsl $tmp | perl -ne 's|<\?xml\s+version.+||;print'
}


to_conf() {
    tmp=$FILE.tmp
    # - replace [screen] to '=remove' (removed at end)
    # - replace [foo] and [/foo] with ''
    # - replace [ foo ] with [foo]
    perl -pe 's/\[(.?)screen\]/=remove/g; s/\[(.?)\w+\]//g; s/\[\s(\w+)\s\]/[$1]/g;' $FILE > $tmp.tmp

    xsl=$DN/conf-xml2conf.xsl
    perl -pe "s|%\{name\}|$NAME.conf|g" $xsl > $xsl.tmp

    echo "Writing $NAME.conf"
    xsltproc $xsl.tmp $tmp.tmp |
    # hash text between =xxxstart and =xxxend
    perl -ne 's/^\s+(=xxx\w+)/$1/; $in = 1 if /^=xxxstart/; $in = 0 if /^=xxxend/; if ($in) { s/^\s+([^#])/# $1/; }; print if $_ !~ /^=/' |
    # remove unneeded leading lines and lines with =remove tag
    perl -ne '$print = 1 if /^#/; print if $print && $_ !~ /=remove/' |
    # remove empty lines between [section] and its first param/descr
    perl -ne '$st = 1 if !defined $st and /^#\[\w+\]/; $st = 2 if $st == 1 and /^\s*$/; $st = 0 if $st == 2 and /\w/; print "$_" if !$st or $st != 2' > $NAME.conf
}

TO=$1
FILE=$2
[ -n "$TO" ] || exit 1
[ -n "$FILE" ] || exit 1

xmllint --noout $FILE || exit 1

if [ "$TO" = "man" ]; then
   to_man

elif [ "$TO" = "docb" ]; then
   to_docb

elif [ "$TO" = "conf" ]; then
   NAME=$3
   [ -n "$NAME" ] || exit 1
   to_conf
else 
    echo "unknown target"
    exit 1
fi

[ $? -eq 0 ] && rm -f *.tmp $DN/*.tmp
