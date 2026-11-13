# Prerequisites

The plugins need a C++17 compiler, CMake 3.15 or later, and the MongoDB C (2.x) and C++ (4.x) drivers.

## System packages

The [release binaries](../README.md#supported-platforms) need none of this: they only need glibc 2.28 or later.

### RHEL-like (Oracle Linux 9, Rocky, Alma), as in `.docker/Dockerfile`

```bash
dnf config-manager --enable ol9_addons   # Oracle Linux only
dnf -y install gcc gcc-c++ make cmake patch git curl unzip python3 \
               libuuid-devel openssl-devel cyrus-sasl-devel zlib-devel
```

### Debian-like

```bash
apt -y install build-essential cmake git curl unzip python3 \
               uuid-dev libssl-dev libsasl2-dev zlib1g-dev
```

## MongoDB drivers

The simplest is to let CMake download and build them, with `-DAUTO_INSTALL_DEPENDENCIES=ON` (see [AUTO_CONFIG](./DEPENDENCIES_AUTO_CONFIG.md)). The tested versions are:

- MongoDB C driver (libmongoc and libbson) **2.5.4**; 2.0 at least;
- MongoDB C++ driver (mongocxx and bsoncxx) **4.6.0**; 4.0 at least.

To use drivers installed on the system instead, build them as static libraries with position-independent code:

```bash
curl -fL -o mongo-c-driver-2.5.4.tar.gz https://github.com/mongodb/mongo-c-driver/releases/download/2.5.4/mongo-c-driver-2.5.4.tar.gz
tar -xzf mongo-c-driver-2.5.4.tar.gz
cmake -S mongo-c-driver-2.5.4 -B mongo-c-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local -DENABLE_STATIC=ON -DENABLE_SHARED=OFF -DENABLE_ICU=OFF \
      -DENABLE_TESTS=OFF -DENABLE_EXAMPLES=OFF
cmake --build mongo-c-build -j"$(nproc)" && sudo cmake --install mongo-c-build

curl -fL -o mongo-cxx-driver-r4.6.0.tar.gz https://github.com/mongodb/mongo-cxx-driver/releases/download/r4.6.0/mongo-cxx-driver-r4.6.0.tar.gz
tar -xzf mongo-cxx-driver-r4.6.0.tar.gz
cmake -S mongo-cxx-driver-r4.6.0 -B mongo-cxx-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=OFF -DCMAKE_PREFIX_PATH=/usr/local
cmake --build mongo-cxx-build -j"$(nproc)" && sudo cmake --install mongo-cxx-build
```

Then configure the plugins with `-DLINK_STATIC_LIBS=ON`, and `-DMONGOC_ROOT=/usr/local -DMONGOCXX_ROOT=/usr/local` if CMake does not find them.

## Useful resources

- MongoDB C driver: https://mongoc.org/libmongoc/current/learn/get/installing.html
- MongoDB C++ driver: https://www.mongodb.com/docs/languages/cpp/cpp-driver/current/get-started/
- Orthanc build: https://orthanc.uclouvain.be/book/faq/compiling.html
