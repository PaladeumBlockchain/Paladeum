 #!/usr/bin/env bash

 # Execute this file to install the yona cli tools into your path on OS X

 CURRENT_LOC="$( cd "$(dirname "$0")" ; pwd -P )"
 LOCATION=${CURRENT_LOC%Yona-Qt.app*}

 # Ensure that the directory to symlink to exists
 sudo mkdir -p /usr/local/bin

 # Create symlinks to the cli tools
 sudo ln -s ${LOCATION}/Yona-Qt.app/Contents/MacOS/yonad /usr/local/bin/yonad
 sudo ln -s ${LOCATION}/Yona-Qt.app/Contents/MacOS/yona-cli /usr/local/bin/yona-cli
