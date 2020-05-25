# MongoDB plugins for Orthanc DICOM Server

See: https://github.com/Doc-Cirrus/orthanc-mongodb

## OVERVIEW
The repository contains two plugins to store the data in MongoDB database.

## Installation

This chapter describes the process of installation with not too much details and not necessarily contain all how-to to resolve possible problems that might appear.

### Prerequisites
- mongodb 3.6+ server 
- Install jsoncpp
- Install/build mongoc library http://mongoc.org/libmongoc/current/installing.html
- Install/build mongo-cxx lib https://mongodb.github.io/mongo-cxx-driver/mongocxx-v3/installation/
- Download Ortanc sources or install orthanc-devel package

---
It is possible to install all dependencies manually (below you will find insturctions) or you could try to use ```AUTO_INSTALL_DEPENDENCIES``` flag for cmake. In this way some dependencies will be installed automaticaly for orthanc-mongodb project. For more details refer to ```docs``` folder -> ```autoconfig.md``` file.

### CentOS 7 Build Instructions

## General Packages
```bash
yum -y install centos-release-scl centos-release-scl-rh epel-release
yum -y install make devtoolset-7 libuuid-devel openssl-devel cyrus-sasl-devel cmake3 zlib-devel
```

## Prerequisite: Mongo C Driver 1.15.x
https://github.com/mongodb/mongo-c-driver/releases
```bash
curl -L --output mongo-c-driver-1.15.x.tar.gz https://github.com/mongodb/mongo-c-driver/archive/1.15.x.tar.gz
tar -xzf mongo-c-driver-1.15.x.tar.gz
mkdir -p mongo-c-driver-1.15.x/build
cd mongo-c-driver-1.15.x/build
scl enable devtoolset-7 "cmake3 -DCMAKE_C_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF .."
scl enable devtoolset-7 "make"
scl enable devtoolset-7 "sudo make install"
```

## Prerequisite: MongoDB C++ Driver 3.4.x
https://github.com/mongodb/mongo-cxx-driver/releases
```bash
curl -L --output mongo-cxx-driver-3.4.x.tar.gz https://github.com/mongodb/mongo-cxx-driver/archive/3.4.x.tar.gz
tar -xzf mongo-cxx-driver-3.4.x.tar.gz
mkdir -p mongo-cxx-driver-3.4.x/build
cd mongo-cxx-driver-r3.4.x/build
scl enable devtoolset-7 "cmake3 -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DLIBBSON_DIR=/usr/local -DLIBMONGOC_DIR=/usr/local .."
# for any reason it requires write permissions to /usr/local/include/bsoncxx/v_noabi/bsoncxx/third_party/mnmlstc/share/cmake/core
# so use sudo for make too
scl enable devtoolset-7 "sudo make"
scl enable devtoolset-7 "sudo make install"
```

## Prerequisite: JsonCpp (1.8.0 exact)
```bash
curl -L --output jsoncpp-1.8.0.tar.gz https://github.com/open-source-parsers/jsoncpp/archive/1.8.0.tar.gz
tar -xzf jsoncpp-1.8.0.tar.gz
mkdir -p jsoncpp-1.8.0/build
cd jsoncpp-1.8.0/build
scl enable devtoolset-7 "cmake3 -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release .."
scl enable devtoolset-7 "make"
scl enable devtoolset-7 "sudo make install"
```

## Build of this orthanc-mongodb plugin itself
```bash
mkdir -p orthanc-mongodb/build
cd orthanc-mongodb/build
scl enable devtoolset-7 "cmake3 -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local -DORTHANC_ROOT=/usr/include .."
scl enable devtoolset-7 "make"
scl enable devtoolset-7 "sudo make install"
```

* ```ORTHANC_ROOT``` - the Orthanc server sources root to include the ```orthanc/OrthancCPlugin.h```
* ```BUILD_TESTS``` - option to build tests, default off
* ```BUILD_WITH_GCOV``` - option to include coverage default off

### Debian 9 Build Instructions (static build)

## General Packages
```bash
apt -y install build-essential unzip cmake make libsasl2-dev uuid-dev libssl-dev zlib1g-dev git curl
```

## Prerequisite: Mongo C Driver 1.15.x
https://github.com/mongodb/mongo-c-driver/releases
```bash
curl -L --output mongo-c-driver-1.15.x.tar.gz https://github.com/mongodb/mongo-c-driver/archive/1.15.x.tar.gz
tar -xzf mongo-c-driver-1.15.x.tar.gz
mkdir -p mongo-c-driver-1.15.x/build
cd mongo-c-driver-1.15.x/build
cmake -DCMAKE_C_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DENABLE_STATIC=ON -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF -DENABLE_ICU=OFF ../mongo-c-driver-1.15.x
make
sudo make install
```

## Prerequisite: MongoDB C++ Driver 3.4.x
https://github.com/mongodb/mongo-cxx-driver/releases
```bash
curl -L --output mongo-cxx-driver-3.4.x.tar.gz https://github.com/mongodb/mongo-cxx-driver/archive/3.4.x.tar.gz
tar -xzf mongo-cxx-driver-3.4.x.tar.gz
mkdir -p mongo-cxx-driver-3.4.x/build
cd mongo-cxx-driver-r3.4.x/build
cmake -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DLIBBSON_DIR=/usr/local -DLIBMONGOC_DIR=/usr/local ..
sudo make
sudo make install
```

## Prerequisite: JsonCpp (1.8.0 exact)
```bash
curl -L --output jsoncpp-1.8.0.tar.gz https://github.com/open-source-parsers/jsoncpp/archive/1.8.0.tar.gz
tar -xzf jsoncpp-1.8.0.tar.gz
mkdir -p jsoncpp-1.8.0/build
cd jsoncpp-1.8.0/build
cmake -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```

## Build of this orthanc-mongodb plugin itself
```bash
mkdir -p orthanc-mongodb/build
cd orthanc-mongodb/build

cmake -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr -DLINK_STATIC_LIBS=TRUE -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local ..
make
```

### Plugin configuration setup

Add plugins in the Ortahc json configuration file:

```json
    ...
  "Plugins" : [
    "libOrthancMongoDBIndex.dylib",
    "libOrthancMongoDBStorage.dylib"
  ],
  // MongoDB plugin confihuration section:
  "MongoDB" : {
    "EnableIndex" : true, // false to use default SQLite 
    "EnableStorage" : true, // false to use default SQLite 
    "ConnectionUri" : "mongodb://localhost:27017/orthanc_db",
    "ChunkSize" : 261120
  },
  ...
```

Also it's possible to configure the plugin with separate config options:

```json
    "Plugins" : [
    "libOrthancMongoDBIndex.dylib",
    "libOrthancMongoDBStorage.dylib"
    ],
    "MongoDB" : {
        "host" : "customhost",
        "port" : 27001,
        "user" : "user",
        "database" : "database",
        "password" : "password",
        "authenticationDatabase" : "admin",
        "ChunkSize" : 261120
    }
```

**NOTE: Setting up the ConnectionUri overrides the host, port, database params. So if the ConnectionUri is set, the other parameters except the ChunkSize will be ignored.**

Testing is described with more details in [here](doc/testing.md)

## Known Issues:

1. ConnectionUri must contain the database name due to the [bug in the mongocxx driver CXX-1187](https://jira.mongodb.org/browse/CXX-1187)
2. The Orthanc server should be compiled in Release or RelWithDebInfo mode to turn off assertions. There are asserts like that 
   `./OrthancServer/ServerIndex.cpp:      assert(index_.currentStorageSize_ == index_.db_.GetTotalCompressedSize());``` it involves full collection scan 
   to aggregate the total AttachedFiles significantly slows the performance down. 



