#! /bin/sh

SRCDIR=/home/ftp/RPMSt
SRCURL=http://localhost/RPMSt/

DISTDIR=/mnt/PLD
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
	cp -a /tmp/ftp___localhost_RPMSt/pac* /tmp

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
    toadd=$(perl -e 'print chr(65 + rand(50))');
    torm=$(perl -e 'print chr(65 + rand(50))');
    while [ "$toadd" == "$torm" ]; do
	torm=$(perl -e 'print chr(65 + rand(56))');
    done	

    echo "ADD $toadd, REMOVE $torm";
    rm -f $SRCDIR/${torm}*.rpm
    
    for i in $DISTDIR/${toadd}*.rpm; do
	echo $i;
        bn=$(basename $i);

	if [ ! -f $i ]; then 
	    continue
	fi    

	if [ -f $SRCDIR/$bn ]; then 
	    continue
	fi    
	echo "ADD $bn"
        ln -sf $i $SRCDIR/$bn
    done
 	
    ./poldek -v -s $SRCDIR --mkidxz
    cp -a /tmp/ftp___localhost_RPMSt/pac* /tmp

    echo "UP"
    ./poldek -v -s $SRCURL --up
    if [ $? -ne 0 ]; then
	   echo "ERRROR"
	   exit 1;
    fi
}



./poldek -s $SRCDIR --mkidxz || exit 1
./poldek -s $SRCURL --upa || exit 1

for n in $(seq 1 220); do
    t2
done

