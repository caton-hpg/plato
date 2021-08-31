#!/bin/bash
set -ex

yum update -y && 
yum install -y epel-release \
    autoconf \
    curl \
    curl-devel \
    expat-devel \
    gettext \
    glibc-devel \
    glibc-headers \
    m4 \
    make \
    ncurses-devel \
    openldap-devel \
    openssh-clients \
    openssl-devel \
    perl-JSON \
    perl-PerlIO-gzip \
    perl-Digest-MD5 \
    python3-devel \
    python3-pip \
    readline-devel \
    redhat-lsb-core \
    rpm-build \
    sudo \
    unzip \
    vim \
    wget \
    xz \
    zlib-devel \
    libatomic \
    bzip2-devel \
  && yum clean all \
  && rm -rf /var/cache/yum

if [ "x${VERSION}" != "x8" ]; then yum install -y centos-release-scl; fi

cd /root

# Install cmake 3.15
mkdir -p /opt/vesoft/toolset/cmake \
  && curl -fsSL https://github.com/Kitware/CMake/releases/download/v3.15.5/cmake-3.15.5-Linux-x86_64.tar.gz -O \
  && tar zxf cmake*.tar.gz -C /opt/vesoft/toolset/cmake --strip-components=1 \
  && rm -rf cmake*.tar.gz

export TOOLSET_CLANG_DIR=/opt/vesoft/toolset/clang/10.0.0
export PATH=/opt/vesoft/toolset/cmake/bin:${TOOLSET_CLANG_DIR}/bin:${PATH}
export CC=${TOOLSET_CLANG_DIR}/bin/gcc
export CXX=${TOOLSET_CLANG_DIR}/bin/g++

## SHELL ["/bin/bash", "-c"]

# Install gcc and llvm by nebula-gears
bash <(curl -s https://raw.githubusercontent.com/vesoft-inc/nebula-gears/master/install) --prefix=/opt/vesoft/ \
  && /opt/vesoft/bin/install-llvm --version=10.0.0 \
  && ln -snf ${TOOLSET_CLANG_DIR}/bin/llvm-symbolizer /usr/bin/llvm-symbolizer

# Install git 2.25
wget -qO- https://github.com/git/git/archive/v2.25.0.tar.gz | tar zxf - -C ./ \
  && cd git-2.25.0 \
  && make configure \
  && ./configure --prefix=/usr \
  && make -j$(nproc) && make install \
  && cd ../ && rm -rf git-2.25.0

# Install ccache
wget -qO- https://github.com/ccache/ccache/releases/download/v3.7.7/ccache-3.7.7.tar.gz | tar zxf - -C ./ \
  && cd ccache-3.7.7 \
  && ./configure \
  && make -j \
  && make install \
  && cd ../ && rm -rf ccache-*

# Install lcov to /usr/local/bin, require master to make gcc9 happy
git clone --branch master --single-branch https://github.com/linux-test-project/lcov.git \
  && cd lcov && git checkout 75fbae1cfc5027f818a0bb865bf6f96fab3202da \
  && make install && cd ../ && rm -rf lcov

# fastcov
if [ "x${VERSION}" == "x7" ]; then pip3 install --no-cache-dir fastcov; fi

# Install ossutil64
wget -q http://gosspublic.alicdn.com/ossutil/1.6.10/ossutil64 \
  && mv ossutil64 /usr/bin/ \
  && chmod +x /usr/bin/ossutil64
