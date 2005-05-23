#! /bin/sh

POLDEK="./poldek"
POLDEKUP="./poldek"

while  [ $# -gt 0 ]; do
    case "$1" in
	-t )
	    shift; ST=$1; shift ;;

        --distdir )
    	    shift; DISTDIR="$1"; shift;;

        --sdir )
    	    shift; SRCDIR="$1"; shift;;

        --surl )
    	    shift; SRCURL="$1"; shift;;

	--poldek )
	    shift; POLDEK="$1"; shift;;

	--poldekup )
            shift; POLDEKUP="$1"; shift;;
    esac
done
ST=${ST:-"pdir"}

if [ -z "$DISTDIR" -o -z "$SRCDIR" -o -z "$SRCURL" ]; then
    echo "usage $(basename $0): -t INDEXTYPE --distdir DISTDIR --sdir $SRCDIR --surl $SRCURL"
    exit 1;
fi

TMPDIR=${TMPDIR:-"/tmp"}

t1() 
{
    for i in $DISTDIR/*.rpm; do
        bn=$(basename $i);

	if [ -f $SRCDIR/$bn ]; then 
	    continue
	fi    
	echo ""
	echo "ADD $bn"
        ln -sf $i $SRCDIR/$bn
 	
	$POLDEK -s $SRCDIR --st dir --mkidx --mt $ST

	echo "UP"
	$POLDEK --st $ST -s $SRCURL --up
	if [ $? -ne 0 ]; then
	    echo "ERRROR"
	    exit 1;
	fi
	sleep 1
    done
}

t2() 
{
    up_skip=$(perl -e 'print int(rand(2))');
    toadd=$(perl -e 'print chr(65 + rand(50))');
    torm=$(perl -e 'print chr(65 + rand(50))');
    while [ "$toadd" == "$torm" ]; do
	torm=$(perl -e 'print chr(65 + rand(56))');
    done	
    
    echo "ADD $toadd, REMOVE $torm";


    nremoved=0
    for i in $SRCDIR/${torm}*.rpm; do
        bn=$(basename $i);

	if [ ! -f $i ]; then 
	    continue
	fi
        rm -f $i
        nremoved=$(expr $nremoved + 1)
    done 
	

    nadded=0
    for i in $DISTDIR/${toadd}*.rpm; do
        bn=$(basename $i);

	if [ ! -f $i ]; then 
	    continue
	fi    

	if [ -f $SRCDIR/$bn ]; then 
	    continue
	fi    
	nadded=$(expr $n + 1)
	#echo "ADD $bn"
        ln -sf $i $SRCDIR/$bn
    done

    if [ "$nadded" = "0" -a "$nremoved" = "0" ]; then 
	return 
    fi

    echo -e "\n**** MAKE ****\n"
    echo "Added $nadded and $nremoved removed"	
    $POLDEK -s $SRCDIR --mkidx --mt $ST
    if [ $? -ne 0 ]; then
	   echo "MKIDX ERRROR"
	   exit 1;
    fi 

    #up_skip=0
    if [ ${up_skip} = "0" ]; then
	echo -e "\n**** UP ****\n"
	#$POLDEKUP -v --st $ST -s $SRCURL --up -Oautoupa=n
	$POLDEKUP --noconf -v -s $SRCURL --up
	if [ $? -ne 0 ]; then
	   echo "ERRROR"
	   exit 1;
	fi
    fi	
}

rm -f $SRCDIR/packages.$ST.*
rm -f $SRCDIR/packages.i/packages.$ST.*
$POLDEK -s $SRCDIR --mkidx --mt $ST || exit 1
$POLDEK --st $ST -s $SRCURL --upa || exit 1

for n in $(seq 1 22000); do
    t2
    sleep 1
done

