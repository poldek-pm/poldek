#!/bin/sh
# $Id$
# See help() for description

PATH="/bin:/sbin:/usr/bin:/usr/sbin"
USER=${POLDEKUSER:-"poldek"}
SYSCONFDIR="/etc"
SUDOERS="$SYSCONFDIR/sudoers"

help() {
    echo "Script configures poldek to run as ordinary user when executed by root:"
    echo " - adds user '$USER'"
    echo " - adds to sudoers file permission to '$USER' to run rpm as root"
    echo "   without a password"
    echo " - adds 'runas = $USER' to poldek.conf"
    echo " - copy existing poldek files (/root/.poldekrc and so on) to '$USER' user $HOME"
    echo ""
    echo "Do '$0 --yes' to configure"
}

copyfile() {
    file=$1
    dir=$2

    if [ ! -r $HOME/$file ]; then
        return 1
    fi

    if [ -r $dir/$file ]; then
        echo "$dir/$file exists, $HOME/$file not copied"
        return 1
    else
        echo "Copying $HOME/$file to $dir/..."
        cp -a $HOME/$file $dir && chown -R $USER.$USER $dir/$file
    fi
}

if [ "$(id -u 2>/dev/null)" != "0" ]; then
    echo "You must be root to run me"
    exit 1
fi

ARG=$1
if [ -z "$ARG" -o "$ARG" != "--yes" ]; then
    help
    exit 0
fi        

if [ ! -f $SUDOERS ]; then
    SUDOERS="/sudo/sudoers"
fi

if [ ! -f $SUDOERS ]; then
    echo "$SYSCONFDIR/sudoers nor $SYSCONFDIR/sudo/sudoers not found"
    exit 1
fi 

if [ -n "${POLDEKHOME}" ]; then
    homedir="${POLDEKHOME}"

else
    homedir="/var/cache"
    if [ ! -d $homedir ]; then 
        echo "$homedir: no such directory (non-FHS file system?)"
        exit 1
    fi 
    homedir="$homedir/$USER"
fi

if [ ! -d $homedir ]; then
    mkdir -p -v --mode=755 $homedir || exit 1
fi

if [ -z "$(id -u $USER 2>/dev/null)" ]; then
    echo "Adding user '$USER.$USER' with home directory '$homedir'"
    groupadd -r -f $USER || exit 1
    useradd -r -d $homedir $USER -g $USER -s /bin/sh -c "poldek user" || exit 1
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

if copyfile .poldekrc "$homedir"; then
    copyfile .poldek_history $homedir
    copyfile .poldek-cache $homedir
    copyfile .poldek-aliases.conf $homedir
fi    
homeconfig="$HOME/.poldekrc"
if [ -f $homeconfig ]; then
    echo "$homeconfig saved as ${homeconfig}.save"
    mv -f $homeconfig ${homeconfig}.save
fi

config="/etc/poldek/poldek.conf";
if [ ! -f $config ]; then config="/etc/poldek.conf"; fi

if [ -f $config ]; then
    if ! grep -q "^[ ]*runas[ ]*=[ ]*$USER" $config; then
        echo "Configure $config poldek to run as '$USER'..."
        perl -pi -e "s|^\#?\s*runas\s*=\s*\w+\s*|runas = $USER\n|" $config
    fi
elif [ -z "${UNDERRPM}" ]; then
    echo "Cannot found poldek.conf file, modify it yourself adding:"
    echo "runas = $USER"
fi
    

exit 0
