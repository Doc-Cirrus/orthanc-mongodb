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
| `PORTABLE_BUILD` | `OFF` | Plugins that only need glibc: the MongoDB C driver uses the static OpenSSL installed in `OPENSSL_ROOT_DIR`, no SASL (so no Kerberos) and its own copy of zlib, and libstdc++ and libgcc are linked statically. Needs `STATIC_BUILD` and `AUTO_INSTALL_DEPENDENCIES`. See [Portable build](#portable-build). |
| `ENABLE_COVERAGE` | `OFF` | Instrument the plugins and the unit tests for code coverage (GCC, `--coverage`), see [Testing](./TESTING.md#code-coverage). |
| `ORTHANC_FRAMEWORK_SOURCE` | `archive` | Where to get the Orthanc framework 1.13.0: `archive` (with `ORTHANC_FRAMEWORK_ARCHIVE`, by default `Resources/Orthanc-1.13.0.tar.gz`, the source archive of Orthanc kept in the repository), `web`, `hg` or `path` (with `ORTHANC_FRAMEWORK_ROOT`). |
| `USE_SYSTEM_ORTHANC_SDK` | `ON` | Use the Orthanc SDK headers of the system. With `OFF`, or with `STATIC_BUILD`, the copy of SDK 1.13.0 in `Resources/Orthanc/Sdk-1.13.0` is used. |

## Portable build

The binaries of the releases are built with glibc 2.28 and a recent GCC (C++17), with every other dependency linked statically, so they load on any x86_64 Linux with glibc 2.28 or later, next to any other plugin of Orthanc. The `lsb` stage of `.docker/Dockerfile` does it in the `manylinux_2_28` image (AlmaLinux 8, GCC 14):

```bash
docker build -f .docker/Dockerfile --target lsb-artifacts --output type=local,dest=dist .
```

`dist` then holds `libOrthancMongoDBIndex.so`, `libOrthancMongoDBStorage.so` and `SHA256SUMS`. The stage builds OpenSSL (3.5 LTS) statically, then the plugins with:

```bash
cmake -S MongoDB -B build -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON -DALLOW_DOWNLOADS=ON \
      -DAUTO_INSTALL_DEPENDENCIES=ON -DPORTABLE_BUILD=ON -DOPENSSL_ROOT_DIR=/opt/openssl \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
```

`CMAKE_POLICY_VERSION_MINIMUM` is needed with CMake 4, as some bundled third-party libraries declare an older minimum version. Finally, `Resources/CheckPortability.sh 2.28 <plugins>` checks that the plugins only need the libraries of glibc, use no glibc symbol newer than 2.28, and export nothing but the entry points of an Orthanc plugin.

The static OpenSSL reads the certificate authorities of the system in `/etc/ssl` (`/etc/ssl/cert.pem`, or the hashed `/etc/ssl/certs` folder), which covers the RHEL-like and Debian-like distributions. Elsewhere, give them with the `tlsCAFile` option of the connection URI, or with the `SSL_CERT_FILE` environment variable of Orthanc.

## Docker

The Docker files are in the `.docker` folder. The build context of `.docker/Dockerfile` is the repository root. Its stages:

- `base`: the toolchain and the system libraries (Oracle Linux 9);
- `dev`: an interactive image, used by the `dev` service of the development environment, with the sources mounted from the host;
- `build`: builds and installs the plugins from the build context (`--build-arg BUILD_TYPE=Debug` for a debug build);
- `coverage`: the unit tests built with `ENABLE_COVERAGE`, which run `Resources/Coverage.sh` (see [Testing](./TESTING.md#code-coverage));
- `lsb` and `lsb-artifacts`: the [portable build](#portable-build);
- `runtime`: Orthanc 1.13.0 (Linux Standard Base binaries) with the plugins, Orthanc Explorer 2, the Stone Web Viewer and DICOMweb;
- `integration`: the orthanc-tests suite, run against the portable plugins (see [Testing](./TESTING.md#integration-tests)).

```bash
docker build -f .docker/Dockerfile --target runtime -t orthanc-mongodb .
```

### Compose environments

`.docker/docker-compose.yml` holds what every environment shares: the network, the MongoDB volumes, and two MongoDB 7.0 servers, `database` (standalone) and `database-rs` (single-node replica set `rs0`). Each environment adds its services from an override file:

| Override file | Services |
|---------------|----------|
| `docker-compose.override.dev.yaml.example` | `dev`: development shell, with the sources and the build directory (volume) mounted |
| `docker-compose.override.runtime.yaml.example` | `orthanc`: the `runtime` image, on the standalone server by default; `dicom-sender`: seeds Orthanc with [sample studies](../.docker/DicomSender/README.md) on the first `docker compose up` |
| `docker-compose.override.test.yaml.example` | `unit-tests`: the unit tests against both servers; `integration-tests`: the orthanc-tests suite |

Copy one of them to `docker-compose.override.yaml`, and `.env.example` to `.env`, then run `docker compose` from the `.docker` folder, which loads both files:

```bash
cd .docker
cp .env.example .env
cp docker-compose.override.runtime.yaml.example docker-compose.override.yaml
docker compose up -d
```

In the runtime environment, the first `docker compose up` also seeds Orthanc with the sample studies (`SEED_ENABLED=false` in `.env` to skip it); `docker compose up -d orthanc` starts Orthanc alone. Both copies are git-ignored. Without copying, give the files from the repository root:

```bash
docker compose -f .docker/docker-compose.yml -f .docker/docker-compose.override.test.yaml.example run --rm unit-tests
```

To combine several environments, list their files in `COMPOSE_FILE` in `.env`. The settings of `.env.example` are the versions of the images and downloads, the paths of the Orthanc and PostgreSQL plugin sources for the `dev` shell, and for `orthanc` its ports, its connection URI (`ORTHANC_MONGO_URL`) and the main options of the `MongoDB` section. The paths in these files are relative to the `.docker` folder.

The versions of the downloaded Orthanc binaries are build arguments: `ORTHANC_VERSION` (1.13.0), `ORTHANC_EXPLORER_2_VERSION` (1.15.0), `STONE_WEB_VIEWER_VERSION` (3.0) and `ORTHANC_DICOMWEB_VERSION` (1.24), from the [Linux Standard Base downloads](https://orthanc.uclouvain.be/downloads/linux-standard-base/index.html). The runtime configuration is `Resources/Config/configuration.json`. It reads the MongoDB connection URI from the `MONGO_URL` environment variable, `mongodb://database:27017/inpacs` by default:

```bash
docker run -e MONGO_URL="mongodb://db1:27017/orthanc?replicaSet=rs0" -p 8042:8042 -p 4242:4242 orthanc-mongodb
```

The `MongoDB` options of this file also come from environment variables, with the defaults below. Orthanc writes each value into the JSON as it is, before reading the file, so use `true` or `false` for the Booleans:

| Variable | Option | Default |
|----------|--------|---------|
| `MONGODB_ENABLE_INDEX` | `EnableIndex` | `true` |
| `MONGODB_ENABLE_STORAGE` | `EnableStorage` | `true` |
| `MONGODB_ENABLE_TRANSACTIONS` | `EnableTransactions` | `Auto` |
| `MONGODB_INDEX_CONNECTIONS_COUNT` | `IndexConnectionsCount` | `5` |
| `MONGODB_CREATE_INDEXES_AT_STARTUP` | `CreateIndexesAtStartup` | `true` |
| `MONGODB_ENABLE_EXTENDED_FIND` | `EnableExtendedFind` | `true` |
