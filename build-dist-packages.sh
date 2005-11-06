#! /bin/sh
# $Id$ 

PATH="/bin:/sbin:/usr/bin:/usr/sbin"

ver=$(perl -ne "print \$1 if /^PACKAGE_VERSION='([\d\.]+)'$/" configure)
poldek=poldek-${ver}.tar.bz2
noargs=
distros=$@

if [ -z "$1" ]; then 
        distros="fedora#c3 fedora#c4 pld#ra rh#9.0"
        echo "usage $(basename $0): no distro#ver specified, build for $distros"
	#echo "usage $(basename $0): [distro#ver]"
	#exit 1
        noargs="1"
fi	

echo "Build $poldek..."
for distrospec in $distros; do 
    distro=$(echo $distrospec | awk -F '#' '{ print $1 }');	
    distro_ver=$(echo $distrospec | awk -F '#' '{ print $2 }');
    
    echo "Distro: $distro - $distro_ver"
    sleep 1;
    ddir=/var/${distro}-sys-${distro_ver}
    buildscript=/tmp/build-${poldek}.sh 

    rpm="rpm"
    bcond=""
    #target_def="--define '_target i386'"

    case "$distro" in
        pld)
            if [ "${distro_ver}." == "ra." ]; then 
                bcond="--without xml_metadata"; 
            fi
            ;;
	rh)
	    rpm="rpmbuild"
            bcond="--without xml_metadata"
	    ;;
	fedora)
            rpm="rpmbuild"
	    ;;
	*)
            distro_def="$distro"
    esac

    if [ -z "$distro_def" ]; then distro_def="$distro"; fi

    rpmopt="--define 'distro $distro_def'  $bcond --target i386"
    
    rm -f $ddir/$buildscript
#   disabled
#   if [ "$distro" = "pld" ]; then
#	echo "+ static"    
#	echo "su - mis -c \"$rpm -tb --with static $rpmopt /tmp/$poldek\"" >> $ddir/$buildscript
#   fi

    echo "su - mis -c \"$rpm -ta $rpmopt /tmp/$poldek\""
    echo "su - mis -c \"$rpm -ta $rpmopt /tmp/$poldek\"" >> $ddir/$buildscript
    

    cp ${poldek} $ddir/tmp || exit 1
    /usr/sbin/chroot $ddir sh $buildscript || exit 1
    destdir=/tmp/poldek-$ver/download/${distro}/${distro_ver}
    mkdir -p $destdir

    rpmd=$ddir/home/mis/rpm/RPMS	
    if [ -d $rpmd/i386 ]; then
	rpmd=$rpmd/i386
    fi
    
    cp -v $rpmd/poldek-$ver*.rpm $destdir
    chmod 644 $destdir/*.rpm || true
done
chown -R mis /tmp/poldek-$ver


if [ "$noargs" == "1" ]; then       # no script arguments - build everything
    cp $poldek /tmp/poldek-$ver
    rpm -ts $poldek && cp ~/rpm/SRPMS/poldek-$ver*.src.rpm /tmp/poldek-$ver
fi
#rsync -vrt --rsh=ssh /tmp/poldek-$ver/download/ team.pld.org.pl:public_html/poldek/download
