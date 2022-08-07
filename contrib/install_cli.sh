 #!/usr/bin/env bash

 # Execute this file to install the paladeum cli tools into your path on OS X

 CURRENT_LOC="$( cd "$(dirname "$0")" ; pwd -P )"
 LOCATION=${CURRENT_LOC%Paladeum-Qt.app*}

 # Ensure that the directory to symlink to exists
 sudo mkdir -p /usr/local/bin

 # Create symlinks to the cli tools
 sudo ln -s ${LOCATION}/Paladeum-Qt.app/Contents/MacOS/paladeumd /usr/local/bin/paladeumd
 sudo ln -s ${LOCATION}/Paladeum-Qt.app/Contents/MacOS/paladeum-cli /usr/local/bin/paladeum-cli
