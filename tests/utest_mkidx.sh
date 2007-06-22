#!/bin/sh
# $Id$

# --mkidx test script 

#  Index creation use cases:
#  a) -s /foo                   =>  dir  -> default type
#  b) --st type -s /foo         =>  type -> default type
#  c) --st type /foo --mt type  =>  type -> dtype

#  d) -s /foo --mt dtype        =>  dir  -> dtype
#  e) -s /foo --mt dtype,dtype2 =>  dir  -> dtype

#  f) -n foo                    =>  dir (or original type) -> foo's type

#  g) -s /foo --mkidx=/tmp/bar.gz 
#  h) -n foo --mkidx=/tmp/bar.gz 


QUIET="-q"
VERBOSE=""
if [ -n "$1" -a "$1" == "-v" ]; then VERBOSE="1"; QUIET=""; fi

POLDEK="../cli/poldek $QUIET"
POLDEK_NOCONF="../cli/poldek $QUIET --noconf -Ovfile_retries=1"

DIR="${PWD}/repo"
TMP="${PWD}/tmp"


msg() {
   [ -n "$VERBOSE" ] && echo -e $@
}

exit1() {
    echo $@
    rm -rf tmp
    rm -rf $DIR/packages.*
    exit 1
}

clean() {
    rm -rf $DIR/packages.*
    rm -rf $TMP/*.*
}

# index test_name"
die_if_empty() {
    anindex=$1
    prefix=$2

    [ -f $anindex ] || exit1 "$prefix: $anindex not created"
    anindex_type="pdir"
    if echo $anindex | grep -q \.ndir\.; then anindex_type="pndir"; fi

    anindex_real_type="pdir"
    if zgrep -q '^tndb[0-9]\.[0-9]' $anindex; then
       anindex_real_type="pndir"
    fi
    
    if [ "$anindex_real_type" != "$anindex_type" ]; then
        exit1 "$prefix: invalid $anindex type $anindex_type, real is $anindex_real_type"
    fi

    if zgrep -q 'Contains 0 packages' $anindex; then
        exit1 "$prefix: empty $anindex"
    fi
}



#  a) -s /foo                  =>  dir  -> default type
test_a() 
{
    msg "\nRunning test a)"
    $POLDEK_NOCONF -s $DIR --mkidx || exit1 "test_a: mkidx failed"
    die_if_empty "$DIR/packages.ndir.gz" "test_a"
}

#  b) --st type -s /foo         =>  type -> default type
#  c) --st type /foo --mt type  =>  type -> dtype
test_bc() 
{
    msg "\nPreparing tests bc"
    $POLDEK_NOCONF -s $DIR --mkidx || exit1 "test_bc: mkidx failed"
    die_if_empty "$DIR/packages.ndir.gz" "test_bc"

    rm -rf $TMP/packages.*
    cp $DIR/packages.ndir.* $TMP || exit1 "cp failed"

    
    # c) --st type /foo --mt type =>  type -> dtype
    msg "\nRunning test c)"
    $POLDEK_NOCONF --st pndir -s $TMP --mkidx --mt=pdir || 
        exit1 "test_c: pdir index creation failed"
    die_if_empty "$TMP/packages.dir.gz" "test_c" 

    # b) --st type -s /foo
    msg "\nRunning test b)"
    rm -f $TMP/packages.ndir*
    $POLDEK_NOCONF --st pdir -s $TMP --mkidx || 
        exit1 "test_b: pndir index creation failed"
    die_if_empty "$TMP/packages.ndir.gz" "test_b"
}

#  d) -s /foo --mt dtype        =>  dir  -> dtype
#  e) -s /foo --mt dtype,dtype2 =>  dir  -> dtype
test_de() 
{
    msg "\nRunning test d)"
    $POLDEK_NOCONF -s $DIR --mkidx --mt pdir || exit1 "test_d: mkidx failed"
    die_if_empty "$DIR/packages.dir.gz" "test_d"

    rm -rf $DIR/packages.*
    $POLDEK_NOCONF -s $DIR --st dir --mkidx --mt pdir || exit1 "test_d1: mkidx failed"
    die_if_empty "$DIR/packages.dir.gz" "test_d1"

    clean

    # e)
    msg "\nRunning test e)"
    $POLDEK_NOCONF -s $DIR --mkidx --mt pdir,pndir || exit1 "test_e: mkidx failed"
    die_if_empty "$DIR/packages.ndir.gz" "test_e"
    die_if_empty "$DIR/packages.dir.gz" "test_e"
}

#  f) -n foo                    =>  dir (or original type) -> foo's type
test_f() 
{
    POLDEK_CONF="$POLDEK --conf $DIR/poldek.conf"

    msg "\nRunning test f)"
    $POLDEK_CONF -n repo-pndir --mkidx || exit1 "test_f: mkidx failed"
    die_if_empty "$DIR/packages.ndir.gz" "test_f"

    $POLDEK_CONF -n repo-pdir --mkidx || exit1 "test_f: mkidx failed"
    die_if_empty "$DIR/packages.dir.gz" "test_f"

    clean

    $POLDEK_CONF -n repo-pndir -n repo-pdir --mkidx || exit1 "test_f1: mkidx failed"
    die_if_empty "$DIR/packages.ndir.gz" "test_f1"
    die_if_empty "$DIR/packages.dir.gz" "test_f1"
}

#  g) -s /foo --mkidx=/tmp/bar.gz 
test_g() 
{
    msg "\nRunning test g)"
    POLDEK_CONF="$POLDEK --conf $DIR/poldek.conf"

    $POLDEK_NOCONF -s $DIR --mkidx=$TMP || 
        exit1 "test_g: mkidx failed"
    die_if_empty "$TMP/packages.ndir.gz" "test_g"

    $POLDEK_NOCONF -s $DIR --mkidx=$TMP/packages.foo.ndir.gz || 
        exit1 "test_g1: mkidx failed"
    die_if_empty "$TMP/packages.foo.ndir.gz" "test_g1"


    $POLDEK_NOCONF -s $DIR --mkidx=$TMP --mt pdir || 
        exit1 "test_g2: mkidx failed"
    die_if_empty "$TMP/packages.dir.gz" "test_g2"
    

    $POLDEK_NOCONF -s $DIR --mkidx=$TMP/packages.bar.dir.gz --mt pdir || 
        exit1 "test_g3: mkidx failed"
    die_if_empty "$TMP/packages.bar.dir.gz" "test_g3"

    
    $POLDEK_NOCONF --st pdir -s $TMP/packages.bar.dir.gz \
                   --mkidx=$TMP/packages.foo2.ndir.gz --mt pndir || 
        exit1 "test_g4: mkidx failed"
    die_if_empty "$TMP/packages.foo2.ndir.gz" "test_g4"

    $POLDEK_NOCONF --st pndir -s $TMP/packages.foo2.ndir.gz \
                   --mkidx=$TMP/packages.foo3.ndir.gz --mt pndir || 
        exit1 "test_g5: mkidx failed"
    die_if_empty "$TMP/packages.foo3.ndir.gz" "test_g5"


    clean
    $POLDEK_NOCONF -s $DIR --mkidx --mt pndir,pdir || 
        exit1 "test_g6: mkidx failed"

    # run httpd
    python httpd.py >/dev/null 2>/dev/null &
    
    POLDEK_NOCONF="$POLDEK_NOCONF -Ovfile_retries=1"
    $POLDEK_NOCONF --st pndir -s http://localhost:10000/repo/ --mkidx=$TMP
    die_if_empty "$TMP/packages.ndir.gz" "test_g6"

    $POLDEK_NOCONF --st pndir -s http://localhost:10000/repo/ --mkidx=$TMP --mt pdir
    die_if_empty "$TMP/packages.dir.gz" "test_g6"

    $POLDEK_NOCONF --st pndir -s http://localhost:10000/repo/ --mkidx=$TMP/packages.foo.ndir.gz
    die_if_empty "$TMP/packages.foo.ndir.gz" "test_g6"

    $POLDEK_NOCONF --st pndir -s http://localhost:10000/repo/ --mkidx=$TMP/packages.bar.dir.gz --mt pdir
    die_if_empty "$TMP/packages.bar.dir.gz" "test_g6"

    kill %1
}

#  h) -n foo --mkidx=/tmp/bar.gz 
test_h() 
{
    POLDEK_CONF="$POLDEK --conf $DIR/poldek.conf"

    msg "\nRunning test h)"
    $POLDEK_CONF -n repo-pdir --mkidx=$DIR/packages.foo.ndir.gz --mt pndir || 
        exit1 "test_h: mkidx failed"
    die_if_empty "$DIR/packages.foo.ndir.gz" "test_h"

    $POLDEK_CONF -n repo-pndir --mkidx=$DIR/packages.foo2.ndir.gz --mt pndir || 
        exit1 "test_h1: mkidx failed"
    die_if_empty "$DIR/packages.foo2.ndir.gz" "test_h1"

    $POLDEK_CONF -n repo-pdir --mkidx=$DIR/packages.bar.dir.gz || 
        exit1 "test_h3: mkidx failed"
    die_if_empty "$DIR/packages.bar.dir.gz" "test_h3"
}




[ -n "$PWD" ] || exit1 "no PWD"
rm -rf tmp
mkdir tmp

clean
test_a

clean
test_bc

clean
test_de

clean
test_f

clean
test_g

clean
test_h

clean
rm -rf tmp
