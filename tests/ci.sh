#!/bin/sh

POLDEK_SOURCE_DIR="${POLDEK_SOURCE_DIR:-/var/users/mis/poldek}"
if [ ! -d /poldek ]; then
    echo "$0: run me with docker as:"
    echo " docker run -it -v $POLDEK_SOURCE_DIR:/poldek registry.gitlab.com/pld-linux/pld /bin/sh -c 'cd /poldek && ./tests/ci.sh'"
    exit 1
fi

set -xeu

# rpm bug? "rpm -Uvh coreutils-... linux-libc-headers-..." fails with unmet dep 'coreutils'
poldek --noask -uv findutils coreutils

poldek --noask -uv ca-certificates git-core libstdc++ fakeroot autoconf automake libtool rpm-build-tools bzip2-devel check-devel docbook-dtd412-xml docbook2X gettext-autopoint gettext-tools glibc-devel libgomp-devel libxml2-devel lua54-devel ncurses-devel openssl-devel pcre-devel perl-XML-Simple perl-base perl-modules pkgconfig popt-devel readline-devel rpm-devel tar gzip texinfo xmlto xz zlib-devel zstd-devel #| tee ci-poldek.log

git config --global --add safe.directory /poldek

./autogen.sh

make CFLAGS="-O0"
make CFLAGS="-O0" check
make CFLAGS="-O0" check-sh
