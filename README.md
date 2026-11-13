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

## Upgrading to 1.13: performance

One run of the same workloads on the previous release (Orthanc 1.11.3, `master`) and on this one (Orthanc 1.13.0, the portable binaries), median of 3 passes:

| Workload | 1.11 | 1.13 | Change |
|---|---:|---:|---|
| C-STORE, 1 association | 49.7 instances/s | 57.6 instances/s | 1.2× higher |
| C-STORE, 8 associations | 242 instances/s | 284 instances/s | 1.2× higher |
| Explorer 2 study list (100 studies with tags) | 1,504 ms | 37 ms | 40× faster |
| C-FIND of all the studies | 11.5 s | 0.54 s | 21× faster |
| C-FIND of the MR studies (`ModalitiesInStudy`) | 3.0 s | 0.15 s | 20× faster |
| `GET /statistics` | 54 ms | 1.1 ms | 51× faster |
| **At scale** (100,000 patients, 3,000,000 instances): | | | |
| Explorer 2, latest 100 studies | 3,146 ms | 59 ms | 54× faster |
| Explorer 2, latest 100 studies of a name prefix | 210 s | 62 ms | > 3,000× faster |
| `/tools/find`: CT studies, limit 100 | 2,877 ms | 67 ms | 43× faster |
| `/tools/find`: studies of one week | 796 ms | 133 ms | 6× faster |
| `/tools/find`: MR series of one month | 2,569 ms | 968 ms | 2.7× faster |
| `GET /studies/{id}`, `/series/{id}`, `/patients/{id}` | 1.5–1.6 ms | 1.7–2.0 ms | about the same |
| `GET /instances?limit=100&since=500000` | 97 ms | 132 ms | 1.4× slower |
| **Viewer** (DICOMweb, every frame of an 889-image CT series): | | | |
| 1 viewer | 3.5 s | 4.0 s | 1.1× slower |
| 8 viewers at once | 15.0 s | 20.1 s | 1.3× slower |
| **Deletion** of all the patients (4 threads) | 82 of 476 fail; all 50,864 files stay in GridFS | 26.7 s, no error, nothing left | fixed |

Where the gains come from:

- With the V4 database API, Orthanc hands a whole search (constraints, ordering, limits, requested tags) to the index plugin, which answers it with one MongoDB aggregation instead of one request per resource.
- The plugin starts each search from its most selective constraint, and reads the Explorer 2 "latest studies" lists in the order of an index instead of sorting every match.
- New indexes are built at startup, and statistics counters replace the full scans of `/statistics`.

The viewer rows compare the upgrade as a whole: DICOMweb (1.10 against 1.24) and the Stone Web Viewer (2.5 against 3.0) differ too. With Orthanc 1.13 on both sides, the storage plugin of this release reads a DICOM file as fast as the previous one or faster (2.8–3.1 ms against 3.4–3.7 ms), so the slower frames come from Orthanc 1.13 and DICOMweb 1.24. Orthanc 1.13 also uses more memory under load (a peak of 1.4–1.9 GB against 0.6–1.7 GB), and about 50 MB when idle on both.

Setup, 2026-09-25: WSL2 on 16 CPUs; Orthanc limited to 4 CPUs and 4 GB, MongoDB 7.0 (standalone, as the previous release has no transactions) to 4 CPUs and 6 GB. The workloads ran on the 12 sample sets of the [DICOM sender](./.docker/DicomSender/README.md) (11 modalities) sent 34 times, about 51,000 instances and 22 GB, and the queries at scale on one index-only database from `Resources/MongoDB/Benchmark/GenerateIndex.js`, restored into both sides. Orthanc 1.11 rejects the sample in Deflated transfer syntax, so it held 34 fewer instances.

## Documentation

- [Build prerequisites](./docs/PREREQUISITES.md)
- [Plugin compilation](./docs/PLUGIN_COMPILATION.md)
- [Plugin configuration](./docs/PLUGIN_CONFIGURATION.md)
- [Testing](./docs/TESTING.md)
