#!/bin/bash

set -ex

CUR_DIR=$(realpath $(dirname $0))
ROOT_DIR=$(realpath $CUR_DIR/..)
cd $ROOT_DIR

MAIN="$ROOT_DIR/bazel-bin/example/pagerank" # process name
WNUM=3
WCORES=8

#INPUT=${INPUT:="$ROOT_DIR/data/graph/v100_e2150_ua_c3.csv"}
INPUT=${INPUT:="nebula:$ROOT_DIR/scripts/nebula.conf"}
#INPUT=${INPUT:="$ROOT_DIR/data/graph/non_coding_5_7.csv"}
#INPUT=${INPUT:="$ROOT_DIR/data/graph/raw_graph_10_9.csv"}
OUTPUT=${OUTPUT:="nebula:$ROOT_DIR/scripts/nebula.conf"}
#OUTPUT=${OUTPUT:="/tmp/pagerank"}
IS_DIRECTED=${IS_DIRECTED:=true}
NEED_ENCODE=${NEED_ENCODE:=false}
EPS=${EPS:=0.0001}
DAMPING=${DAMPING:=0.85}
ITERATIONS=${ITERATIONS:=100}

# param
PARAMS+=" --threads ${WCORES}"
PARAMS+=" --input ${INPUT} --output ${OUTPUT} --is_directed=${IS_DIRECTED} --need_encode=${NEED_ENCODE}"
PARAMS+=" --iterations ${ITERATIONS} --eps ${EPS} --damping ${DAMPING}"

# mpich
MPIRUN_CMD=${MPIRUN_CMD:="$ROOT_DIR/3rd/mpich/bin/mpiexec.hydra"}

# test
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:$ROOT_DIR/3rd/hadoop2/lib

# output dir
if  [[ $OUTPUT != nebula:* ]] ;
then
    if [ -d ${OUTPUT} ]; then
        rm -rf $OUTPUT
    fi
    mkdir -p $OUTPUT
fi

# create log dir if it doesn't exist
LOG_DIR=$ROOT_DIR/logs
if [ -d ${LOG_DIR} ]; then
    rm -rf $LOG_DIR
fi
mkdir -p ${LOG_DIR}

# run
${MPIRUN_CMD} -n ${WNUM} ${MAIN} ${PARAMS} --log_dir=$LOG_DIR

echo ">>>>>>>>>>>output>>>>>>>>>>>>>>"
if  [[ $OUTPUT != nebula:* ]] ;
then
    ls -lh $OUTPUT
fi
