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
    ```sh
    yum install centos-release-scl-rh
    yum install devtoolset-3-gcc devtoolset-3-gcc-c++
    ```
* download, compile, install cmake
    ```sh
    wget https://cmake.org/files/v3.7/cmake-3.7.2.tar.gz
    tar zxf cmake-3.7.2.tar.gz
    cd cmake-3.7.2
    # enable gcc 4.9
    scl enable devtoolset-3 bash
    ./configure
    make
    gmake
    sudo make install
    ```

* download, compile and install boost
    ```sh
        wget https://sourceforge.net/projects/boost/files/boost/1.63.0/boost_1_63_0.tar.gz/download -O boost_1_63_0.tar.gz
        tar zxf boost_1_63_0.tar.gz
        cd boost_1_63_0
        ./bootstrap.sh
        ./b2 cflags=-fPIC
        sudo ./b2 install
    ```
* donwload builld and install mongo c driver
    ```
    wget ...
    tar zxf ...
    cd ...
    ./configure --enable-static
    make
    sudo make install
    ```
* download and build mongo cxx driver
    ```sh
    curl -OL https://github.com/mongodb/mongo-cxx-driver/archive/r3.1.1.tar.gz
    tar -xzf r3.1.1.tar.gz
    cd mongo-cxx-driver-r3.1.1/build
    export CXXFLAGS=-fPIC
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DLIBBSON_DIR=/usr/local -DLIBMONGOC_DIR=/usr/local ..
    sudo make
    sudo make install
    ```

## Build Configuration

1. Download the package ```___.tar.gz```
2. Unpack the sources, and go to the source catalog
3. create folder ```mkdir build``` and ```cd build```
4. Linux only: ```export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/```
5. Run cmake and build the project
    ```sh
    cmake ..
    make
    ```

### Build Configuration options:
* ```ORTHANC_ROOT``` - the Orthanc server sources root to include the ```orthanc/OrthancCPlugin.h```

To run cmake with the custom congiguration use the command liek that:
```sh
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

## Known Issues:

1. ConnectionUri must contain the database name due to the [bug in the mongocxx driver CXX-1187](https://jira.mongodb.org/browse/CXX-1187)
2. 