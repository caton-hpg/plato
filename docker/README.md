# Nebula Graph Development Docker Image

Use `docker` to build [*plato*](https://github.com/vesoft-inc-private/plato) project.

At this moment, we have provided following docker images for developers:

- `vesoft/nebula-plato-dev:centos7`
- `vesoft/nebula-plato-dev:centos8`
- `vesoft/nebula-plato-dev:ubuntu1604`
- `vesoft/nebula-plato-dev:ubuntu1804`
- `vesoft/nebula-plato-dev:ubuntu2004`

## Usage

At first, you should install `docker` in your machine and then pull the [`vesoft/nebula-plato-dev:centos7`](https://hub.docker.com/r/vesoft/nebula-plato-dev) image from docker hub.
After that you can use following commands to build `plato` sources.

    $ docker pull vesoft/nebula-plato-dev:centos7
    $ chmod +x run.sh
    $ ./run.sh /path/to/plato/directory centos7


## make docker images 
```
docker pull vesoft/nebula-dev:ubuntu1604
docker pull vesoft/nebula-dev:ubuntu1804
docker pull vesoft/nebula-dev:ubuntu2004
docker pull vesoft/nebula-dev:centos7
docker pull vesoft/nebula-dev:centos8

cd nebula-plato-dev
docker build -t vesoft/nebula-plato-dev:ubuntu1604  --build-arg VERSION=ubuntu1604 -f Dockerfile.ubuntu ./
docker build -t vesoft/nebula-plato-dev:ubuntu1804  --build-arg VERSION=ubuntu1804 -f Dockerfile.ubuntu ./
docker build -t vesoft/nebula-plato-dev:ubuntu2004  --build-arg VERSION=ubuntu2004 -f Dockerfile.ubuntu ./
docker build -t vesoft/nebula-plato-dev:centos7  --build-arg VERSION=centos7 -f Dockerfile.centos ./
docker build -t vesoft/nebula-plato-dev:centos8  --build-arg VERSION=centos8 -f Dockerfile.centos ./

docker push vesoft/nebula-plato-dev:ubuntu1604
docker push vesoft/nebula-plato-dev:ubuntu1804
docker push vesoft/nebula-plato-dev:ubuntu2004
docker push vesoft/nebula-plato-dev:centos7
docker push vesoft/nebula-plato-dev:centos8

```