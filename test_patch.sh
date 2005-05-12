#! /bin/sh

POLDEK="./poldek"
s=$1
ST=${s:-"pdir"}

inhome=
if [ -n "$inhome" ]; then
    SRCDIR=/home/ftp/RPMSt
    SRCURL=ftp://localhost/RPMSt/

    DISTDIR=/mnt/PLD
else

    SRCDIR=/home/httpd/html/RPMSt
    SRCURL=http://localhost/RPMSt/

    DISTDIR=/var/www/common-html/fedora/Fedora/RPMS/
fi

TMPDIR=/tmp


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
 	
	$POLDEK -s $SRCDIR --st dir --mkidx --mkidx-type $ST

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
    $POLDEK -s $SRCDIR --mkidx --mkidx-type $ST
    if [ $? -ne 0 ]; then
	   echo "MKIDX ERRROR"
	   exit 1;
    fi 

    #up_skip=0
    if [ ${up_skip} = "0" ]; then
	echo -e "\n**** UP ****\n"
	$POLDEK -v --st $ST -s $SRCURL --up -Oautoupa=n
	if [ $? -ne 0 ]; then
	   echo "ERRROR"
	   exit 1;
	fi
    fi	
}

rm -f $SRCDIR/packages.$ST.*
rm -f $SRCDIR/packages.i/packages.$ST.*
$POLDEK -s $SRCDIR --mkidx --mkidx-type $ST || exit 1
$POLDEK --st $ST -s $SRCURL --upa || exit 1

for n in $(seq 1 22000); do
    t2
    sleep 1
done

