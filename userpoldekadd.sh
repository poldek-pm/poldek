#!/bin/sh

PATH="/bin:/sbin:/usr/bin:/usr/sbin"
USER=poldek
SYSCONFDIR="/etc"
SUDOERS="$SYSCONFDIR/sudoers"

if [ ! -f $SUDOERS ]; then
    SUDOERS="/sudo/sudoers"
fi

if [ ! -f $SUDOERS ]; then
    echo "$SYSCONFDIR/sudoers nor $SYSCONFDIR/sudo/sudoers not found"
    exit 1
fi 


homedir=/var/cache
if [ ! -d $homedir ]; then homedir=/var/lib; fi
if [ ! -d $homedir ]; then 
    echo "$homedir: no such directory (non-FHS file system?)"
    exit
fi 

homedir="$homedir/$USER"
mkdir -p -v --mode=700 $homedir || exit 1

if [ -z "$(id -u $USER 2>/dev/null)" ]; then
    echo "Adding user '$USER.$USER' with $homedir HOME"
    groupadd -r -f $USER || exit 1
    useradd -r -d $homedir poldek -g poldek -s /bin/sh -c "poldek user" || exit 1
fi

chown -R $USER.$USER $homedir

if ! grep -q "^poldek" $SUDOERS; then
    rpm=$(which --skip-alias --skip-dot --skip-tilde --skip-functions rpm 2>/dev/null)
    if [ -z "$rpm" ]; then echo "rpm: command not found"; exit 1; fi 
    line="poldek ALL=(ALL) NOPASSWD: $rpm"
    echo "Adding '$line' to $SUDOERS"
    echo $line >> $SUDOERS || exit 1
    echo "Notice: $SUDOERS file changed"
fi

echo "Remember to add line 'runas = $USER' to poldek configuration file!"
