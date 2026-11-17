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

The Docker files are in the `.docker` folder. The build context of `.docker/Dockerfile` is the repository root. Its stages:

- `base`: the toolchain and the system libraries (Oracle Linux 9);
- `dev`: an interactive image, used by the `dev` service of the development environment, with the sources mounted from the host;
- `build`: builds and installs the plugins from the build context (`--build-arg BUILD_TYPE=Debug` for a debug build);
- `runtime`: Orthanc 1.13.0 (Linux Standard Base binaries) with the plugins, Orthanc Explorer 2, the Stone Web Viewer and DICOMweb.

```bash
docker build -f .docker/Dockerfile --target runtime -t orthanc-mongodb .
```

### Compose environments

`.docker/docker-compose.yml` holds what every environment shares: the network, the MongoDB volumes, and two MongoDB 7.0 servers, `database` (standalone) and `database-rs` (single-node replica set `rs0`). Each environment adds its services from an override file:

| Override file | Services |
|---------------|----------|
| `docker-compose.override.dev.yaml.example` | `dev`: development shell, with the sources and the build directory (volume) mounted |
| `docker-compose.override.runtime.yaml.example` | `orthanc`: the `runtime` image, on the standalone server by default; `dicom-sender`: seeds Orthanc with [sample studies](../.docker/DicomSender/README.md) on the first `docker compose up` |
| `docker-compose.override.test.yaml.example` | `unit-tests`: the unit tests against both servers |

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
