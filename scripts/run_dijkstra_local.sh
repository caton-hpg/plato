#!/bin/bash

set -ex

CUR_DIR=$(realpath $(dirname $0))
PROJECT=$(realpath $CUR_DIR/..)
cd $PROJECT

MAIN="$PROJECT/bazel-bin/example/dijkstra" # process name
WNUM=1
WCORES=6

#INPUT=${INPUT:="$PROJECT/data/graph/v100_e2150_ua_c3.csv"}
INPUT=${INPUT:="nebula:$PROJECT/scripts/nebula.conf"}
#INPUT=${INPUT:="$PROJECT/data/graph/non_coding_5_7.csv"}
#INPUT=${INPUT:="$PROJECT/data/graph/raw_graph_10_9.csv"}
OUTPUT=${OUTPUT:="nebula:$PROJECT/scripts/nebula.conf"}
#OUTPUT=${OUTPUT:="/tmp/dijkstra"}
IS_DIRECTED=${IS_DIRECTED:=true}
# vid encoder
NEED_ENCODE=${NEED_ENCODE:=false}
VTYPE=${VTYPE:=string}
ENCODER=${ENCODER:="distributed"}

ITERATIONS=${ITERATIONS:=10}
SOURCE=${SOURCE:="100"}

# param
PARAMS+=" --threads ${WCORES}"
PARAMS+=" --input ${INPUT}  --is_directed=${IS_DIRECTED}  --vtype=${VTYPE} --need_encode=${NEED_ENCODE} --source=${SOURCE}"
PARAMS+=" --iterations ${ITERATIONS} --output ${OUTPUT}"

# mpich
MPIRUN_CMD=${MPIRUN_CMD:="$PROJECT/3rd/mpich/bin/mpiexec.hydra"}

# test
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:$PROJECT/3rd/hadoop2/lib

# output dir
if  [[ $OUTPUT != nebula:* ]] ;
then
    if [ -d ${OUTPUT} ]; then
        rm -rf $OUTPUT
    fi
    mkdir -p $OUTPUT
fi

# create log dir if it doesn't exist
LOG_DIR=$PROJECT/logs
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