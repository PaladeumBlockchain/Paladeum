 #!/usr/bin/env bash

 # Execute this file to install the akila cli tools into your path on OS X

 CURRENT_LOC="$( cd "$(dirname "$0")" ; pwd -P )"
 LOCATION=${CURRENT_LOC%Akila-Qt.app*}

 # Ensure that the directory to symlink to exists
 sudo mkdir -p /usr/local/bin

 # Create symlinks to the cli tools
 sudo ln -s ${LOCATION}/Akila-Qt.app/Contents/MacOS/akilad /usr/local/bin/akilad
 sudo ln -s ${LOCATION}/Akila-Qt.app/Contents/MacOS/akila-cli /usr/local/bin/akila-cli
