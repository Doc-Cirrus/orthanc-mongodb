<!-- @format -->

# MongoDB database plugins for Orthanc DICOM Server

## Overview

The repository contains two plugins that store the data of [Orthanc](https://www.orthanc-server.com/) in MongoDB:

- the **index** plugin (`libOrthancMongoDBIndex.so`) replaces the SQLite index: resources, DICOM tags, metadata, changes, labels...;
- the **storage** plugin (`libOrthancMongoDBStorage.so`) stores the DICOM files and the other attachments in GridFS.

They follow the layout of the Orthanc [PostgreSQL plugin](https://orthanc.uclouvain.be/hg/orthanc-databases/) 11.0, with MongoDB in place of SQL.

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
