#! /bin/sh

POLDEK_TESTING=1
export POLDEK_TESTING

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

        *) echo "$1: nknown option"; exit 1; shift ;;
    esac
done

ST=${ST:-"pndir"}

if [ -z "$DISTDIR" -o -z "$SRCDIR" -o -z "$SRCURL" ]; then
    echo "Usage: $(basename $0): [-t INDEXTYPE] --distdir DISTDIR --sdir SRCDIR --surl SRCURL [--poldek POLDEKBIN]"
    echo "poldek diff/patch tester, script performs:

while true do 
   copy/remove random packages from DISTDIR to SRCDIR 
   poldek -s SRCDIR --mkidx 
   poldek -s SRCURL --up 
done

SRCURL is a remotely accessible SRCDIR of course.
Example:
$ test_patch.sh --distdir /cdrom/PLD/RPMS --srcdir=/srv/www/html/rpms --srcurl=http://localhost/html/rpms \\
     --poldek=~/poldek/cli/poldek
"
    exit 1;
fi

TMPDIR=${TMPDIR:-"/tmp"}
POLDEK=${POLDEK:-"poldek"}
[ -z "$POLDEKUP" ] && POLDEKUP="$POLDEK"

echo "Using poldek=$POLDEK, poldekup=$POLDEKUP"

POLDEK018=
if $POLDEK --noconf --version | grep -q 0.18; then
      POLDEK018="1"
fi

POLDEKUP018=
if $POLDEKUP --noconf --version | grep -q 0.18; then
      POLDEKUP018="1"
fi


create_index() 
{
  if [ -n "$POLDEK018" ]; then
      $POLDEK --noconf -s $SRCDIR --mkidxz 
  else 
      $POLDEK --noconf -s $SRCDIR --mkidx --mt $ST
  fi
}

update_index() 
{
  if [ -n "$POLDEKUP018" ]; then
     $POLDEKUP --noconf -s $SRCURL --up
  else 
     $POLDEKUP --noconf --st $ST -s $SRCURL --up -Oautoupa=n
  fi
}


test_loop() 
{
    echo -e "\n**** Changing repo"

    nchanges=0
    nremoved=0
    nadded=0
    while [ "$nchanges" = "0" ]; do 
      up_skip=$(perl -e 'print int(rand(2))');
      toadd=$(perl -e 'print chr(65 + rand(50))');
      torm=$(perl -e 'print chr(65 + rand(50))');
      while [ "$toadd" == "$torm" ]; do
          torm=$(perl -e 'print chr(65 + rand(56))');
      done
    
      echo "- adding $toadd\*, removing $torm\*";


      for i in $SRCDIR/${torm}*.rpm; do
          bn=$(basename $i);
          
          if [ ! -f $i ]; then 
              continue
          fi
          rm -f $i
          nremoved=$(expr $nremoved + 1)
      done 
	
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
      nchanges=$(expr $nchanges + $nadded + $nremoved)
    done

    if [ "$nadded" = "0" -a "$nremoved" = "0" ]; then 
	return 
    fi

    echo "Added $nadded and $nremoved removed"
    echo -e "\n**** Creating index"

    create_index

    if [ $? -ne 0 ]; then
	   echo "MKIDX ERRROR"
	   exit 1;
    fi 

    #up_skip=0
    if [ ${up_skip} = "0" ]; then
	echo -e "\n**** Updating on client side"
    update_index

	if [ $? -ne 0 ]; then
	   echo "ERRROR"
	   exit 1;
	fi
    fi	
}

rm -f $SRCDIR/packages.$ST.*
rm -f $SRCDIR/packages.i/packages.$ST.*
create_index || exit 1

if [ -n "$POLDEKUP018" ]; then
  $POLDEKUP --noconf -s $SRCURL --upa
else 
  $POLDEKUP --noconf --st $ST -s $SRCURL --upa -Oautoupa=n
fi

for n in $(seq 1 22000); do
    test_loop
    sleep 1
done

