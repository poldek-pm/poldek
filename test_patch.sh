#! /bin/sh

inhome=
if [ -n "$inhome" ]; then
    SRCDIR=/home/ftp/RPMSt
    SRCURL=ftp://localhost/RPMSt/

    DISTDIR=/mnt/PLD
else

    SRCDIR=/home/httpd/html/RPMSt
    SRCURL=http://localhost/RPMSt/

    DISTDIR=/mnt/nest-test/i686/
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
 	
	./poldek -s $SRCDIR --mkidxz

	echo "UP"
	./poldek -s $SRCURL --up
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
    echo "Added $nadded and $nremoved removed"	
    ./poldek -s $SRCDIR --mkidxz
    if [ $? -ne 0 ]; then
	   echo "MKIDX ERRROR"
	   exit 1;
    fi 

    #up_skip=0
    if [ ${up_skip} = "0" ]; then
	echo -e "\n**** UP ****\n"
	./poldek -v -s $SRCURL --up
	if [ $? -ne 0 ]; then
	   echo "ERRROR"
	   exit 1;
	fi
    fi	
}

rm -rf $SRCDIR/packages.*
./poldek -s $SRCDIR --mkidxz || exit 1
./poldek -s $SRCURL --upa || exit 1

for n in $(seq 1 220); do
    t2
done

