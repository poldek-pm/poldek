#! /bin/sh

md5() {
	typeset fn=$1
    md5sum $1 | awk '{print $1}'
}

files_eq() {
	typeset fn0=$1
	typeset fn1=$2

	md0=$(md5 $fn0)
	md1=$(md5 $fn1)
#	echo -e "\n$md0 $fn0\n$md1 $fn1" >&2
	if [ "$md0" != "$md1" ]; then
		return 1
	fi
	return 0
}

TMPDIR=${TMPDIR:-"/tmp"}

do_test() {
	typeset dir=$1
	typeset fn=$2
	typeset url=$3

	[ -d $dir ] || mkdir $dir
	cp -a $fn $dir || return 1
	
	bn=$(basename $fn)
	url="$url/$bn"
	./vfget $url $TMPDIR || return 1
	
	files_eq $TMPDIR/$bn $dir/$bn
	return $?
}
#

file=$0
uri=$(basename $0)


run_test() {
	no=$1; shift;
	
	echo -n "test $no $@.."
	$@ # > $TMPDIR/$(basename 0).log
	if [ $? -eq 0 ]; then
		echo "OK"
	else 
		echo "FAILED"
	fi
}

HTTP_DIR=/home/httpd/html/tmp
FTP_DIR=/home/ftp/incoming/tmp


run_test "01" "do_test" $HTTP_DIR $file http://localhost/tmp
#run_test "02" "do_test" $FTP_DIR $file ftp://localhost/incoming/tmp
