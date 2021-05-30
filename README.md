# MongoDB plugins for Orthanc DICOM Server

See: https://github.com/Doc-Cirrus/orthanc-mongodb

## OVERVIEW
The repository contains two plugins to store the data in MongoDB database.

## Installation

This chapter describes the process of installation with not too much details and not necessarily contain all how-to to resolve possible problems that might appear.

### Prerequisites
- mongodb 3.6+ server 
- conan package manager
- Install jsoncpp 1.17.4
- Install/build mongoc library http://mongoc.org/libmongoc/current/installing.html
- Install/build mongo-cxx lib https://mongodb.github.io/mongo-cxx-driver/mongocxx-v3/installation/
- Download Ortanc sources or install orthanc-devel package

## Automatic build using script

On Debian/Ubuntu update compiler using install_gcc_9.sh script.

After run build.sh script.

build.sh arg1 arg2
  - arg1 - version of Orthanc service( 1.9.1 or another )
  - arg2 - build type (Release/Debug)

For example
build.sh 1.9.1 Release

## Manual build

### CentOS 7 Build Instructions

## General Packages
```bash
yum -y install centos-release-scl centos-release-scl-rh epel-release
yum -y install make devtoolset-9 libuuid-devel openssl-devel cyrus-sasl-devel cmake3 zlib-devel python3-pip
pip3 install conan
conan profile update settings.compiler.libcxx=libstdc++11 default
conan profile update settings.compiler.libcxx=libstdc++11 default
```

## Build of this orthanc-mongodb plugin itself
```bash
mkdir -p orthanc-mongodb/build
cd orthanc-mongodb/build
scl enable devtoolset-9 "conan install .."
scl enable devtoolset-9 "cmake3 -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/local -DORTHANC_ROOT=/usr/include .."
scl enable devtoolset-9 "make"
scl enable devtoolset-9 "sudo make install"
```

* ```ORTHANC_ROOT``` - the Orthanc server sources root, which contains the extracted Orthanc package
* ```BUILD_TESTS``` - option to build tests, default off

### CentOS/RHEL 8 Build Instructions

## General Packages
```bash
apt -y install build-essential unzip cmake make libsasl2-dev uuid-dev libssl-dev zlib1g-dev git curl python3
apt -y install python3-pip
pip3 install conan
conan profile update settings.compiler.libcxx=libstdc++11 default
```

## Build of this orthanc-mongodb plugin
```bash
mkdir -p orthanc-mongodb/build
cd orthanc-mongodb/build
conan install ..
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



