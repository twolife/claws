#!/bin/bash

PERL_SCRIPT="claws-mail-kdeservicemenu.pl"
DESKTOP_TEMPLATE="template_claws-mail-attach-files.desktop"
DESKTOP="claws-mail-attach-files.desktop"
SERVICEMENU_DIR="share/apps/konqueror/servicemenus"

function check_environ {
echo "Checking for kde-config..."
if [ -z "$(type 'kde-config' 2> /dev/null)" ]; then
  echo "kde-config not found, checking for \$KDEDIR to compensate..."
  if [ ! -z $KDEDIR ]; then
    export PATH=$PATH:$KDEDIR/bin
  else
    KDEDIR=$(kdialog --title "Where is KDE installed?" --getexistingdirectory / )
    test -z $KDEDIR && exit 1
    export PATH=$PATH:$KDEDIR/bin
  fi
fi
echo "OK"
}

function install_all {
echo "Generating $DESKTOP ..."
SED_PREFIX=${PREFIX//\//\\\/}
sed "s/SCRIPT_PATH/$SED_PREFIX\\/bin\\/$PERL_SCRIPT/" $DESKTOP_TEMPLATE > $DESKTOP
echo "Installing $PREFIX/$SERVICEMENU_DIR/$DESKTOP"
mv -f $DESKTOP $PREFIX/$SERVICEMENU_DIR/$DESKTOP
echo "Installing $PREFIX/bin/$PERL_SCRIPT"
cp -f $PERL_SCRIPT $PREFIX/bin/
echo "Setting permissions ..."
chmod 0644 $PREFIX/$SERVICEMENU_DIR/$DESKTOP
chmod 0755 $PREFIX/bin/$PERL_SCRIPT
echo "Finished installation."
kdialog --msgbox "Finished installation."
}

function uninstall_all {
echo "Removing $PREFIX/$SERVICEMENU_DIR/$DESKTOP"
rm $PREFIX/$SERVICEMENU_DIR/$DESKTOP
echo "Removing $PREFIX/bin/$PERL_SCRIPT"
rm $PREFIX/bin/$PERL_SCRIPT
echo "Finished uninstall."
kdialog --msgbox "Finished uninstall."
}

function show_help {
    echo "Usage: $0 [--global|--local|--uninstall-global|--uninstall-local]"
    echo
    echo "    --global            attempts a system-wide installation."
    echo "    --local             attempts to install in your home directory."
    echo "    --uninstall-global  attempts a system-wide uninstallation."
    echo "    --uninstall-local   attempts to uninstall in your home directory."
    echo
    exit 0
}

if [ -z $1 ]
    then option="--$(kdialog --menu "Please select installation type" \
				local "install for you only" \
				global "install for all users" \
				uninstall-local "uninstall for you only" \
				uninstall-global "uninstall for all users"  2> /dev/null)"
    else option=$1
fi

case $option in
  "--global" )
    check_environ
    PREFIX=$(kde-config --prefix)
    echo "Installing in $PREFIX ..."
    if [ "$(id -u)" != "0" ]; then
	exec kdesu "$0 --global"
    fi
    install_all
    ;;
  "--local" )
    check_environ
    PREFIX=$(kde-config --localprefix)
    echo "Installing in $PREFIX ..."
    if [ ! -d $PREFIX/bin ]; then
      mkdir $PREFIX/bin
    fi
    if [ ! -d $PREFIX/$SERVICEMENU_DIR ]; then
      mkdir $PREFIX/$SERVICEMENU_DIR
    fi
    install_all
    ;;
  "--uninstall-global" )
    check_environ
    PREFIX=$(kde-config --prefix)
    echo "Uninstalling in $PREFIX ..."
    if [ "$(id -u)" != "0" ]; then
	exec kdesu "$0 --uninstall-global"
    fi
    uninstall_all
    ;;
  "--uninstall-local" )
    check_environ
    PREFIX=$(kde-config --localprefix)
    echo "Uninstalling in $PREFIX ..."
    uninstall_all
    ;;
  "-h" )
    show_help
    ;;
  "--help" )
    show_help
    ;;
  * )
    show_help
esac

echo "Done."
