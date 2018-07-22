#!/bin/bash

./compile.sh || exit 1

sudo cp -i build/jpegreader /bin/jpegreader

read -p "Installation complete, would you like to read the help? ([Y]/n) " ln
if [[ "$ln" == "" ]] || [[ "${ln:0:1}" == "Y" ]] || [[ "${ln:0:1}" == "y" ]]
then
	echo && /bin/jpegreader --help
fi
