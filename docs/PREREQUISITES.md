# Prerequisites

The host system will need som dependencies preinstall, which are needed by orthanc/mongoc/mongocxx

## General system dependencies
### Centos like

```bash
yum -y install centos-release-scl centos-release-scl-rh epel-release
yum -y install make devtoolset-8 libuuid-devel openssl-devel cyrus-sasl-devel cmake3 zlib-devel
```

### Debian like
```bash
apt -y install build-essential unzip cmake make libsasl2-dev uuid-dev libssl-dev zlib1g-dev git curl
```

## Note
It's highly recommended to Use the ```AUTO_INSTALL_DEPENDENCIES``` while building, 
since it will take care of all the dependencies but at a performance cost.
Check [AUTO_CONFIG](./DEPENDENCIES_AUTO_CONFIG.md) for mo details.

## MongoC library
The mongoc library needs to precompiled in order to be linked against the db plugin
https://github.com/mongodb/mongo-c-driver/releases

### Centos like
```bash
curl -L --output mongo-c-driver-1.23.2.tar.gz https://github.com/mongodb/mongo-c-driver/archive/1.23.2.tar.gz
tar -xzf mongo-c-driver-1.23.2.tar.gz
mkdir -p mongo-c-driver-1.23.2/build
cd mongo-c-driver-1.23.2/build
scl enable devtoolset-8 "cmake3 -DCMAKE_C_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF .."
scl enable devtoolset-8 "make"
scl enable devtoolset-8 "sudo make install"
```

### Debian like
```bash
# Static build
curl -L --output mongo-c-driver-1.23.2.tar.gz https://github.com/mongodb/mongo-c-driver/archive/1.23.2.tar.gz
tar -xzf mongo-c-driver-1.23.2.tar.gz
mkdir -p mongo-c-driver-1.23.2/build
cd mongo-c-driver-1.23.2/build
cmake -DCMAKE_C_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DENABLE_STATIC=ON -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF -DENABLE_ICU=OFF ../mongo-c-driver-1.23.2
make
sudo make install
```

## MongoCXX library
The mongocxx library needs to precompiled in order to be linked against the db plugin
https://github.com/mongodb/mongo-cxx-driver/releases

### Centos like
```bash
curl -L --output mongo-cxx-driver-3.7.0.tar.gz https://github.com/mongodb/mongo-cxx-driver/archive/3.7.0.tar.gz
tar -xzf mongo-cxx-driver-3.7.0.tar.gz
mkdir -p mongo-cxx-driver-3.7.0/build
cd mongo-cxx-driver-r3.7.0/build
scl enable devtoolset-8 "cmake3 -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DLIBBSON_DIR=/usr/local -DLIBMONGOC_DIR=/usr/local .."
# for any reason it requires write permissions to /usr/local/include/bsoncxx/v_noabi/bsoncxx/third_party/mnmlstc/share/cmake/core
# so use sudo for make too
scl enable devtoolset-8 "sudo make"
scl enable devtoolset-8 "sudo make install"
```

### Debian like
```bash
# static compilation
curl -L --output mongo-cxx-driver-3.7.0.tar.gz https://github.com/mongodb/mongo-cxx-driver/archive/3.7.0.tar.gz
tar -xzf mongo-cxx-driver-3.7.0.tar.gz
mkdir -p mongo-cxx-driver-3.7.0/build
cd mongo-cxx-driver-r3.7.0/build
cmake -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DLIBBSON_DIR=/usr/local -DLIBMONGOC_DIR=/usr/local ..
sudo make
sudo make install
```

## Useful resources
- Mongoc library http://mongoc.org/libmongoc/current/installing.html
- Mongo-cxx library https://mongodb.github.io/mongo-cxx-driver/mongocxx-v3/installation/
- Orthanc build https://hg.orthanc-server.com/orthanc/file/tip/INSTALL
