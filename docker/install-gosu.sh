
#!/bin/bash

set -ex

GOSU_VERSION=1.11

wget -qo /usr/local/bin/gosu "https://github.com/tianon/gosu/releases/download/$GOSU_VERSION/gosu-amd64"
chmod +x /usr/local/bin/gosu
gosu --version
gosu nobody true
