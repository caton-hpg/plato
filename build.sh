#!/bin/bash

set -ex

ROOT_DIR=$(realpath $(dirname $0))
cd $ROOT_DIR

if [[ $1 == "clean" ]]; then
    bazel clean
    ./3rdtools.sh distclean
    ./3rdtools.sh install
fi

export CC=${ROOT_DIR}/3rd/mpich/bin/mpicxx
export BAZEL_LINKOPTS=-static-libstdc++
export LD_LIBRARY_PATH=/opt/vesoft/toolset/clang/10.0.0/lib64/:${ROOT_DIR}/3rd/hadoop2/lib:${ROOT_DIR}/3rd/nebula-cpp/lib64:${LD_LIBRARY_PATH}

# test
bazel test --sandbox_writable_path=$HOME/.ccache plato/...
# bazel test plato/...

# build
bazel build --sandbox_writable_path=$HOME/.ccache example/...
# bazel build example/...
