# Plugin Compilation

The prerequisites are listed in [Build Prerequisites](./PREREQUISITES.md).

## Build

```bash
git clone https://github.com/Doc-Cirrus/orthanc-mongodb.git
cmake -S orthanc-mongodb/MongoDB -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DSTATIC_BUILD=ON \
      -DALLOW_DOWNLOADS=ON \
      -DAUTO_INSTALL_DEPENDENCIES=ON
cmake --build build -j"$(nproc)"
sudo cmake --install build   # into /usr/local/share/orthanc/plugins/
```

This builds `libOrthancMongoDBIndex.so`, `libOrthancMongoDBStorage.so` and the `UnitTests` program (see [Testing](./TESTING.md)).

## CMake options

| Option | Default | Description |
|---|---|---|
| `STATIC_BUILD` | `OFF` | Build the third-party libraries of the Orthanc framework (Boost, JsonCpp, protobuf...) from their sources, instead of using the system ones. |
| `ALLOW_DOWNLOADS` | `OFF` | Allow CMake to download the sources it needs. Required by `STATIC_BUILD` and `AUTO_INSTALL_DEPENDENCIES`. |
| `AUTO_INSTALL_DEPENDENCIES` | `OFF` | Download and build the MongoDB drivers (see [AUTO_CONFIG](./DEPENDENCIES_AUTO_CONFIG.md)). |
| `LINK_STATIC_LIBS` | `OFF` | Link against the static libraries of MongoDB drivers installed on the system. |
| `MONGOC_ROOT`, `MONGOCXX_ROOT` | | Installation prefixes of the system drivers, if CMake does not find them. |
| `BUILD_TESTS` | `ON` | Build the `UnitTests` program. |
| `ORTHANC_FRAMEWORK_SOURCE` | `web` | Where to get the Orthanc framework 1.13.0: `web`, `hg`, `archive` (with `ORTHANC_FRAMEWORK_ARCHIVE`) or `path` (with `ORTHANC_FRAMEWORK_ROOT`). |
| `USE_SYSTEM_ORTHANC_SDK` | `ON` | Use the Orthanc SDK headers of the system. With `OFF`, or with `STATIC_BUILD`, the copy of SDK 1.13.0 in `Resources/Orthanc/Sdk-1.13.0` is used. |

## Docker

The `Dockerfile` has four stages:

- `base`: the toolchain and the system libraries;
- `dev`: an interactive image, used by the `dev` service of `compose.yaml` with the sources mounted from the host;
- `build`: builds and installs the plugins from the build context (`--build-arg BUILD_TYPE=Debug` for a debug build);
- `runtime`: Orthanc 1.13.0 (Linux Standard Base binaries) with the plugins, Orthanc Explorer 2, the Stone Web Viewer and DICOMweb.

```bash
docker build --target runtime -t orthanc-mongodb .
docker compose up orthanc database
```

`compose.yaml` also starts two MongoDB 7.0 servers for the tests: `database` (standalone) and `database-rs` (single-node replica set `rs0`).
