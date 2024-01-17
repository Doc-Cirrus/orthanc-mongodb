# Plugin Compilation

## Basic Build Example
In order to compile the plugin, the prerequisites needs to be available (check [link](./PREREQUISITES.md) for mor details).
You need either to download the master or the tagged release from GitHub and uncompressed it, here an example using curl:

```bash
# Replace "LATEST_TAG" with last tag in github
curl -L --output orthanc-mongodb.tar.gz https://github.com/Doc-Cirrus/orthanc-mongodb/archive/LATEST_TAG.tar.gz
tar -xzf orthanc-mongodb.tar.gz
```

### Centos like
```bash
mkdir -p orthanc-mongodb/build
cd orthanc-mongodb/build
scl enable devtoolset-8 "cmake3 -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_PREFIX_PATH=/usr/local -DSTATIC_BUILD=ON -DCMAKE_BUILD_TYPE=Release -DAUTO_INSTALL_DEPENDENCIES=ON ../MongoDB/"
scl enable devtoolset-8 "make"
scl enable devtoolset-8 "sudo make install"
```

### Debian like
```bash
mkdir -p orthanc-mongodb/build
cd orthanc-mongodb/build

cmake -DCMAKE_INSTALL_PREFIX=/usr -DSTATIC_BUILD=ON -DCMAKE_BUILD_TYPE=Release -DAUTO_INSTALL_DEPENDENCIES=ON ../MongoDB/
make
```

## Cmake Configuration Arguments
* ```AUTO_INSTALL_DEPENDENCIES``` - Automatically build and compile dependencies (mongoc/mongocxx).
* ```ORTHANC_FRAMEWORK_SOURCE``` - (not required) Orthanc server sources with theis values ("system", "hg", "web", "archive" or "path"), check [link](../Resources/Orthanc/CMake/DownloadOrthancFramework.cmake) for more info.
* ```BUILD_TESTS``` - option to build tests, default off
* ```BUILD_WITH_GCOV``` - option to include coverage default off

## Docker

There is a docker image ready to build, with two targets
* Build: compile the plugin `--target build`
* Run: start orthanc with the plugin enabled (`--target runtime`), plus some other one (for testing purpuses).
```
$ docker build --network host --target runtime -t orthanc-mongodb-run .
$ docker build --network host -p 127.0.0.1:8042:8042 -p 127.0.0.1:4242:4242 --target runtime -t orthanc-mongodb-run --name orthanc-mongodb .
```

Link to mongodb instance will need adjusting, for now it require a mongod docker container instance (see [more info](https://hub.docker.com/_/mongo));