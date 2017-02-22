# MongoDB plugins for Orthanc DICOM Server

## OVERVIEW
The repository contains two plugins to store the data in MongoDB database.

## Installation

This chapter describes the process of installation with not too much detals and not nescessarily contain all howto to resolve possible problems that might appear.

### Prerequisites
- Install boost (compile or with package manager)
- Install jsoncpp
- Install/build mongoc library http://mongoc.org/libmongoc/current/installing.html
- Install/build mongo-cxx lib https://mongodb.github.io/mongo-cxx-driver/mongocxx-v3/installation/
- Download Ortanc sources

### CENTOS 6.8 dev environment setup

* install pyton
    ```yum install python```
* install devtoolset-3
    ```
    yum install centos-release-scl-rh
    yum install devtoolset-3-gcc devtoolset-3-gcc-c++
    ```
* download, compile, install cmake
    ```
    wget https://cmake.org/files/v3.7/cmake-3.7.2.tar.gz
    tar zxf cmake-3.7.2.tar.gz
    cd cmake-3.7.2
    # enable gcc 4.9
    scl enable devtoolset-3 bash
    ./configure
    make
    sudo make install
    ```

* download, compile and install boost
    ```
    wget https://sourceforge.net/projects/boost/files/boost/1.63.0/boost_1_63_0.tar.gz/download -O boost_1_63_0.tar.gz
    tar zxf boost_1_63_0.tar.gz
    cd boost_1_63_0
    ./bootstrap.sh
    ./b2 cflags=-fPIC -j 4
    sudo ./b2 install
    ```
* download and install jsoncpp
    ```
    wget https://github.com/open-source-parsers/jsoncpp/archive/1.8.0.tar.gz -O jsoncpp-1.8.0.tar.gz
    tar zxf jsoncpp-1.8.0.tar.gz
    cd jsoncpp-1.8.0
    mkdir build && cd build
    export CXXFLAGS=-fPIC
    cmake ..
    make -j4
    sudo make install
    ```

* donwload and install Orthanc
    ```
    wget http://www.orthanc-server.com/downloads/get.php?path=/orthanc/Orthanc-1.2.0.tar.gz - O Orthanc-1.2.0.tar.gz
    tar zxf Orthanc-1.2.0.tar.gz
    cd Orthanc-1.2.0
    mkdir build
    cd build
    sudo yum install libuuid-devel libpng-devel libjpeg-devel
    cmake -DALLOW_DOWNLOADS=ON -DSTATIC_BUILD=ON ..
    ```

* donwload builld and install mongo c driver
    ```
    wget https://github.com/mongodb/mongo-c-driver/releases/download/1.6.0/mongo-c-driver-1.6.0.tar.gz
    tar zxf mongo-c-driver-1.6.0.tar.gz
    cd mongo-c-driver
    ./configure --enable-static
    make
    sudo make install
    ```
* download and build mongo cxx driver
    ```
    wget https://github.com/mongodb/mongo-cxx-driver/archive/r3.1.1.tar.gz
    tar -xzf r3.1.1.tar.gz
    cd mongo-cxx-driver-r3.1.1/build
    export CXXFLAGS=-fPIC
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DLIBBSON_DIR=/usr/local -DLIBMONGOC_DIR=/usr/local ..
    sudo make
    sudo make install
    ```

## Build Configuration

1. Download and unpack the package ```___.tar.gz``` or clone repository
2. create folder ```mkdir build``` and ```cd build```
3. Linux only: ```export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/```
4. Run cmake and build the project
    ```
    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
    cmake ..
    make
    ```

### Build Configuration options:
* ```ORTHANC_ROOT``` - the Orthanc server sources root to include the ```orthanc/OrthancCPlugin.h```
* ```BUILD_TESTS``` - option to build tests, default off
* ```BUILD_WITH_GCOV``` - option to inclode coverage default off

To run cmake with the custom congiguration use the command liek that:
```
cmake -DORTHANC_ROOT=~/sources/Orthanc-1.2.0 ..
```


## Plugin configuration setup

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
2. 
