<!-- @format -->

# MongoDB database plugins for Orthanc DICOM Server

[![Version](https://img.shields.io/github/v/release/Doc-Cirrus/orthanc-mongodb?sort=semver&label=version)](https://github.com/Doc-Cirrus/orthanc-mongodb/releases)
[![Coverage](https://codecov.io/gh/Doc-Cirrus/orthanc-mongodb/branch/master/graph/badge.svg)](https://codecov.io/gh/Doc-Cirrus/orthanc-mongodb)
[![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue.svg)](./LICENSE.md)

## Overview

The repository contains two plugins that store the data of [Orthanc](https://www.orthanc-server.com/) in MongoDB:

- the **index** plugin (`libOrthancMongoDBIndex.so`) replaces the SQLite index: resources, DICOM tags, metadata, changes, labels...;
- the **storage** plugin (`libOrthancMongoDBStorage.so`) stores the DICOM files and the other attachments in GridFS.

They follow the layout of the Orthanc [PostgreSQL plugin](https://orthanc.uclouvain.be/hg/orthanc-databases/) 11.0, with MongoDB in place of SQL.

## Supported platforms

The binaries of the [releases](https://github.com/Doc-Cirrus/orthanc-mongodb/releases) run on:

- **Linux x86_64 with glibc 2.28 or later**: RHEL, Oracle Linux, Rocky Linux and AlmaLinux 8 or later, Debian 10 or later, Ubuntu 20.04 or later, and the other distributions of the same age. `ldd --version` prints the version of glibc.
- **Orthanc 1.13.0 or later**, including the [Linux Standard Base binaries](https://orthanc.uclouvain.be/downloads/linux-standard-base/index.html) of Orthanc.
- **MongoDB 7.0 or later**.

They only need glibc: OpenSSL, the MongoDB drivers and the C++ runtime are built in. As they are built without SASL, they do not support Kerberos (GSSAPI) authentication; SCRAM, X.509 and TLS work. On an older system or another architecture, build the plugins from the sources ([Plugin compilation](./docs/PLUGIN_COMPILATION.md)).

## Requirements

- Orthanc **1.13.0** or later. The index plugin uses the V4 database API, including labels, extended find, key-value stores, queues, attachment custom data and audit logs.
- MongoDB server **7.0** or later. A replica set (even a single node) is recommended, as it gives each Orthanc transaction a MongoDB transaction. A standalone server works too, with the limits described in the configuration guide.

## Quick start

```json
"Plugins" : [
  "/usr/local/share/orthanc/plugins/libOrthancMongoDBIndex.so",
  "/usr/local/share/orthanc/plugins/libOrthancMongoDBStorage.so"
],
"MongoDB" : {
  "EnableIndex" : true,
  "EnableStorage" : true,
  "ConnectionUri" : "mongodb://localhost:27017/orthanc?replicaSet=rs0"
}
```

The connection can also be given as separate options (`host`, `port`, `database`, `user`, `password`, `authenticationDatabase`), as described in the [configuration guide](./docs/PLUGIN_CONFIGURATION.md#connection-as-separate-options).

With Docker, the `.docker` folder starts Orthanc with both plugins, Orthanc Explorer 2, the Stone Web Viewer and DICOMweb, and a MongoDB 7.0 server. Orthanc is then on http://localhost:8042:

```bash
cd .docker
cp .env.example .env
cp docker-compose.override.runtime.yaml.example docker-compose.override.yaml
docker compose up -d
```

The first `docker compose up` also fills Orthanc with open-source sample studies of 11 modalities, about 650 MB ([DICOM sender](./.docker/DicomSender/README.md)); the next ones do not. To skip it, set `SEED_ENABLED=false` in `.env`, or start Orthanc alone with `docker compose up -d orthanc`. The development and test environments are described in [Plugin compilation](./docs/PLUGIN_COMPILATION.md#compose-environments).

## Upgrading

Databases of the previous releases are kept as they are: the upgrade only adds indexes and collections, and the previous release still runs on an upgraded database. On a large database, build the new indexes offline first, as described in the [configuration guide](./docs/PLUGIN_CONFIGURATION.md#upgrading-from-a-previous-release). The changes are listed in [MongoDB/NEWS](./MongoDB/NEWS).

If you skipped the [1.9.1](https://github.com/Doc-Cirrus/orthanc-mongodb/tree/1.9.1) tag, run its migration section first, unless this is a fresh installation.

## Documentation

- [Build prerequisites](./docs/PREREQUISITES.md)
- [Plugin compilation](./docs/PLUGIN_COMPILATION.md)
- [Plugin configuration](./docs/PLUGIN_CONFIGURATION.md)
- [Testing](./docs/TESTING.md)
