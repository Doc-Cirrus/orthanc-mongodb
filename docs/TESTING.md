# Testing

## Unit tests

`UnitTests` (built with `BUILD_TESTS=ON`, the default) takes the URI of a MongoDB server, without database name. Each test creates a random `test_<uuid>` database and drops it afterwards, so the server can be shared. Run it against a standalone server and against a replica set, as some tests only apply to one of them:

```bash
./UnitTests "mongodb://database:27017/"
./UnitTests "mongodb://database-rs:27017/?replicaSet=rs0"
```

In the `dev` service of the development environment (`.docker/docker-compose.override.dev.yaml.example`), both URIs are in `$MONGO_URL` and `$MONGO_RS_URL`. The test environment builds the tests and runs them against both servers:

```bash
docker compose -f .docker/docker-compose.yml -f .docker/docker-compose.override.test.yaml.example run --rm unit-tests
```

The tests cover:

- the shared index tests of the PostgreSQL plugin (`IndexUnitTests.h`), with the patient-protection and remaining-ancestor sections enabled;
- the storage area (GridFS), including range reads;
- one regression test per fixed bug, the schema revisions and the upgrade from a database of the previous releases;
- concurrency on a standalone server: sequences, `CreateInstance`, statistics, queues, and stores that race the deletion of their patient (`ConcurrentStoreAndDelete`);
- `LookupResources` and `ExecuteFind`/`ExecuteCount`, compared with the expected answers of Orthanc.

## Integration tests

The [orthanc-tests](https://orthanc.uclouvain.be/hg/orthanc-tests/) suite, at the tag `Orthanc-1.13.0`, runs against Orthanc 1.13.0 with both plugins, with the built-in SQLite index as the baseline:

1. Clone the suite with Mercurial: `hg clone -u Orthanc-1.13.0 https://orthanc.uclouvain.be/hg/orthanc-tests/`.
2. Generate the configuration of the Orthanc under test with `GenerateConfigurationForTests.py`, add the `Plugins` and `MongoDB` sections, and start Orthanc with it.
3. Run `Tests/Run.py --force --orthanc <path to Orthanc>`. The suite is Python 2 code: run it with Python 2.7 (e.g. from conda-forge) and the modules `httplib2`, `numpy` and `pydicom` 1.4.

`Run.py` also starts a second Orthanc, the peer of the tests, from the default configuration of the Orthanc binary. The defaults of 1.13.0 have no `"ExecuteLuaEnabled"` key, so that peer refuses `/tools/execute-script`: add the key where `Run.py` sets it to `true`.

## Load tests and sample data

[`.docker/DicomSender`](../.docker/DicomSender/README.md) downloads open-source sample studies of 11 modalities (CT, PT, MR, NM, CR, DX, MG, US, XA, RF, and compressed transfer syntaxes), and sends them to Orthanc by C-STORE as many times as needed, as new studies of new patients:

```bash
.docker/DicomSender/fetch-samples.py
.docker/DicomSender/dicom-sender.py --host orthanc-host --port 4242 --copies 100 --processes 8 --quiet --report load-test.json
```

The report gives the instances per second and the store latencies. In the runtime environment of `.docker`, the `dicom-sender` service seeds Orthanc with the samples.

After a load test, check the Orthanc and MongoDB logs for errors, and check a few studies in Orthanc Explorer 2 or the Stone Web Viewer. To delete all the patients:

```bash
curl -s http://localhost:8042/patients | python3 -c "import json,sys; print('\n'.join(json.load(sys.stdin)))" | xargs -I {} curl -s -X DELETE http://localhost:8042/patients/{}
```

## Test results

2026-09-24, Debug build, Orthanc 1.13.0, MongoDB 7.0, orthanc-tests `Orthanc-1.13.0` (248 tests):

| Index | Unit tests | orthanc-tests failures |
|-------|-----------|------------------------|
| Built-in SQLite (baseline) | — | 5 |
| MongoDB replica set | 79 passed | 5, the same as SQLite |
| MongoDB standalone | 79 passed | 6: the 5 of SQLite, and `test_query_retrieve_format` |

- The 5 failures of the baseline come from the environment: `dciodvfy` is missing (`test_bitbucket_issue_46`, `test_extended_media`, `test_media_archive`, `test_media_encodings`), and `test_lua_deadlock` checks a transcoding.
- `test_query_retrieve_format` fails on a standalone server only. With `"OverwriteInstances" : true` and `"SynchronousCMove" : false`, a C-FIND can run between the deletion and the re-creation of an overwritten instance, and find nothing. This is the limit described in the [configuration guide](./PLUGIN_CONFIGURATION.md#standalone-server-or-replica-set).
- After the suite, both databases are empty, and the statistics equal a full scan. No attachment, GridFS file, child or tag is orphaned. The only exception is 2 orphaned metadata on the standalone server, which were written after their instance had been deleted.
