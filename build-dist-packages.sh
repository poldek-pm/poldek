#! /bin/sh
# $Id$ 

PATH="/bin:/sbin:/usr/bin:/usr/sbin"

#rm -f *.tar.gz
#make dist

ver=$(perl -ne "print \$1 if /^PACKAGE_VERSION='([\d\.]+)'$/" configure)
poldek=poldek-${ver}.tar.bz2

if [ -z "$1" ]; then 
	echo "usage $(basename $0): [pld rh]"
	exit 1
fi	

for distro in $@; do 
    echo "Distro: $distro"
    ddir=/var/${distro}-sys
    buildscript=/tmp/build-${poldek}.sh 

    rpm="rpm"
    #target_def="--define '_target i386'"

    case "$distro" in
	rh)
	    distro_def="rh"
	    rpm="rpmbuild"
	    ;;
	fedora)
    	    distro_def="fedora"
            rpm="rpmbuild"
	    ;;
	rh8)
	    distro_def="rh"
	    rpm="rpm"
	    ;;    
	*)
            distro_def="$distro"
    esac
    

    rpmopt="--define 'distro $distro_def' --target i386"
    
    rm -f $ddir/$buildscript
    if [ "$distro" = "pld" ]; then
	echo "+ static"    
	echo "su - mis -c \"$rpm -tb --with static $rpmopt /tmp/$poldek\"" >> $ddir/$buildscript
    fi
    
    echo "su - mis -c \"$rpm -ta $rpmopt /tmp/$poldek\"" >> $ddir/$buildscript

    cp ${poldek} $ddir/tmp || exit 1
    /usr/sbin/chroot $ddir sh $buildscript || exit 1
    destdir=/tmp/poldek-$ver/${distro}
    mkdir -p $destdir || true

    rpmd=$ddir/home/mis/rpm/RPMS	
    if [ -d $rpmd/i386 ]; then
	rpmd=$rpmd/i386
    fi
    
    cp -v $rpmd/poldek-$ver*.rpm $destdir
    #cp -v $rpmd/home/mis/rpm/SRPMS/poldek-$ver*.rpm $destdir
    chmod 644 $destdir/*.rpm

done

