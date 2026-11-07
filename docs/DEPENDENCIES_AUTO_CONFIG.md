# Automatic build of the MongoDB drivers

With the CMake option `AUTO_INSTALL_DEPENDENCIES=ON`, CMake downloads, builds and installs the MongoDB drivers into the build directory, then links the plugins statically against them:

- MongoDB C driver (libmongoc and libbson) `MONGO_C_VERSION`: **2.5.4**
- MongoDB C++ driver (mongocxx and bsoncxx) `MONGO_CXX_VERSION`: **4.6.0**

These versions are set in [Resources/MongoDB/AutoConfig.cmake](../Resources/MongoDB/AutoConfig.cmake). Other 2.x and 4.x releases should work, but are not tested.

Only the system libraries the drivers depend on must be installed beforehand (OpenSSL, Cyrus SASL, zlib; see [Build Prerequisites](./PREREQUISITES.md)).

```bash
cmake -S MongoDB -B build -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON \
      -DALLOW_DOWNLOADS=ON -DAUTO_INSTALL_DEPENDENCIES=ON
cmake --build build -j"$(nproc)"
```

The first configuration takes a few minutes, as it builds both drivers.
