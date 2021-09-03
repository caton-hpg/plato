#!/bin/bash

set -ex

. /etc/os-release

debian_packages=(
    ca-certificates
    # unzip
    zip
    # gcc
    # g++
    # make
    # autoconf
    automake
    libtool
    pkg-config
    # wget
    # curl
    libnuma-dev
    zlib1g-dev
)

redhat_packages=(
    ca-certificates
    which
    numactl-devel
    # unzip
    zip
    zlib-devel
    # gcc
    # gcc-c++
    # libstdc++-static
    # make
    libtool
    # wget
    # curl
)

function install_bazel() {
    BAZEL_INSTALLED=True
    command -v bazel > /dev/null 2>&1 || { BAZEL_INSTALLED=False; }
    if [ $BAZEL_INSTALLED = True ]; then
        echo "bazel has already installed."
        return;
    fi
    BAZEL_VERSION=1.2.0
    BAZEL_INSTALLER_FIL=bazel-$BAZEL_VERSION-installer-linux-x86_64.sh
    wget -q "https://github.com/bazelbuild/bazel/releases/download/$BAZEL_VERSION/$BAZEL_INSTALLER_FIL"
    chmod +x $BAZEL_INSTALLER_FIL
    ./$BAZEL_INSTALLER_FIL
    rm $BAZEL_INSTALLER_FIL
}

if [ "$ID" = "ubuntu" ] || [ "$ID" = "debian" ]; then
    apt-get install -y "${debian_packages[@]}"
elif [ "$ID" = "centos" ] || [ "$ID" = "fedora" ] || [ "$ID" = "tlinux" ]; then
    yum install -y "${redhat_packages[@]}"
else
    echo "Your system ($ID) is not supported by this script. Please install dependencies manually."
    exit 1
fi

install_bazel
