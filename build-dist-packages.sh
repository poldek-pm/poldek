#! /bin/sh
# $Id$ 

#make dist || exit 1
PATH="/bin:/sbin:/usr/bin:/usr/sbin"
ver=$(perl -ne 'print $1 if /^VERSION=([\d\.]+)$/' configure.in)
poldek=poldek-${ver}.tar.gz

for distro in pld rh; do 
    echo "Distro: $distro"
    ddir=/var/${distro}-sys
    buildscript=/tmp/build-${poldek}.sh 

    rpm="rpm"
    target_def="--target=i386"
    #target_def="--define '_target i386'"

    if [ "${distro}" = "rh" ]; then # broken popt aliases; haven't time to invastigate"
	rpm="/usr/lib/rpm/rpmb"
    fi

    rpm="$rpm --define 'distro ${distro}'"
    rm -f $ddir/$buildscript
    if [ "$distro" = "pld" ]; then
	echo "+ static"    
	echo "su - mis -c \"$rpm $target_def -tb /tmp/$poldek --with static\"" >> $ddir/$buildscript
    fi
    
    echo "su - mis -c \"$rpm $target_def -tb /tmp/$poldek\"" >> $ddir/$buildscript

    cp poldek-${ver}.tar.gz $ddir/tmp || exit 1
    /usr/sbin/chroot $ddir sh $buildscript || exit 1
    destdir=/tmp/poldek-$ver-rpms/${distro}
    mkdir -p $destdir || true

    rpmd=$ddir/home/mis/rpm/RPMS	
    if [ -d $ddir/home/mis/rpm/RPMS/i386 ]; then
	rpmd=$ddir/home/mis/rpm/RPMS/i386
    fi
    
    cp -v $rpmd/poldek-$ver*.rpm $destdir
done

