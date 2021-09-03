#!/usr/bin/env bash

TAG=${2:-latest}

docker run --rm -ti \
  --security-opt seccomp=unconfined \
  -v "$1":/home/plato \
  -w /home/plato \
  --name nebula_$USER \
  vesoft/nebula-plato-dev:$TAG \
  bash
