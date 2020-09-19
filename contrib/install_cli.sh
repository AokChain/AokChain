 #!/usr/bin/env bash

 # Execute this file to install the aokchain cli tools into your path on OS X

 CURRENT_LOC="$( cd "$(dirname "$0")" ; pwd -P )"
 LOCATION=${CURRENT_LOC%AokChain-Qt.app*}

 # Ensure that the directory to symlink to exists
 sudo mkdir -p /usr/local/bin

 # Create symlinks to the cli tools
 sudo ln -s ${LOCATION}/AokChain-Qt.app/Contents/MacOS/aokchaind /usr/local/bin/aokchaind
 sudo ln -s ${LOCATION}/AokChain-Qt.app/Contents/MacOS/aokchain-cli /usr/local/bin/aokchain-cli
