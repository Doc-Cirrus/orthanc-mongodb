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

## Code coverage

`Resources/Coverage.sh` runs the unit tests built with `ENABLE_COVERAGE=ON` against both servers, then writes the coverage of the code of this repository (`MongoDB/` and `Framework/`, without the tests) with [gcovr](https://gcovr.com/): `coverage.xml` (Cobertura) and `coverage.txt` (summary). The `coverage` stage of `.docker/Dockerfile` builds the tests and runs the script:

```bash
docker build -f .docker/Dockerfile --target coverage -t orthanc-mongodb-coverage .
docker compose -f .docker/docker-compose.yml up -d --wait database database-rs
docker run --rm --network orthanc-mongodb -v "$PWD/coverage:/out" orthanc-mongodb-coverage \
       "mongodb://database:27017/" "mongodb://database-rs:27017/?replicaSet=rs0" /out
```

The CI uploads the report to [Codecov](https://codecov.io/gh/Doc-Cirrus/orthanc-mongodb), which draws the coverage badge of the README. The MongoDB drivers and the third-party libraries are not instrumented.

## Integration tests

The [orthanc-tests](https://orthanc.uclouvain.be/hg/orthanc-tests/) suite, at the tag `Orthanc-1.13.0`, runs against Orthanc 1.13.0 (LSB binary) with the portable plugins. The `integration` stage of `.docker/Dockerfile` holds all of it: the suite (with the patch `.docker/integration/orthanc-tests.patch`), Python 2.7 from conda-forge with `httplib2`, `numpy`, Pillow and `pydicom` 1.4, the DCMTK tools that the tests call (`storescu`, `findscu`...), the locale `en_US.UTF-8` (without it, Orthanc cannot match names regardless of accents), Orthanc, and the plugins. Its entry point, `.docker/integration/run-orthanc-tests.sh`, generates the configuration of the Orthanc under test with `GenerateConfigurationForTests.py`, adds the `Plugins` and `MongoDB` sections, starts Orthanc, and runs `Tests/Run.py`:

```bash
docker compose -f .docker/docker-compose.yml -f .docker/docker-compose.override.test.yaml.example run --rm integration-tests
docker compose -f .docker/docker-compose.yml -f .docker/docker-compose.override.test.yaml.example run --rm \
       -e MODE=replica-set -e MONGO_URL="mongodb://database-rs:27017/orthanc_tests?replicaSet=rs0" integration-tests
```

`MONGO_URL` must name an empty database. The logs of Orthanc and of the suite are written to `.docker/integration-results/`. The script fails if a test fails that is not listed in `.docker/integration/known-failures.txt`, if a listed test passes (the list must then be updated), or if the database is not empty after the suite. A test marked `flaky` in the list fails in some runs only: it may pass or fail, and the script reports which.

The patch adds the `"ExecuteLuaEnabled"` key to the configuration of the peer that `Run.py` starts: the defaults of Orthanc 1.13.0 do not have it, so the peer refuses `/tools/execute-script`.

## Continuous integration

`.github/workflows/ci.yml` runs on the pull requests, on the pushes to `master` and `orthanc-1.13`, and on the tags:

| Job | What it does |
|---|---|
| `unit-tests` | Builds the `coverage` stage, runs the unit tests against a standalone server and a replica set, and uploads the coverage to Codecov (secret `CODECOV_TOKEN`). |
| `lsb-build` | Builds the portable plugins (`lsb-artifacts` stage), checks them with `Resources/CheckPortability.sh`, loads them in Orthanc on AlmaLinux 8, Debian 12, Ubuntu 24.04 and Oracle Linux 9, and keeps them as the artifact `orthanc-mongodb-lsb-<version>`. |
| `integration-tests` | Runs orthanc-tests against these same plugins, on a standalone server and on a replica set. |
| `release` | On a tag, once the three other jobs pass: creates the GitHub release (a pre-release for `*-rc*`, `*-beta*` and `*-alpha*` tags), with the plugins, `SHA256SUMS`, and the section of `MongoDB/NEWS` for the version as notes. |

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
- `test_query_retrieve_format` fails on a standalone server only. With `"OverwriteInstances" : true` and `"SynchronousCMove" : false`, a C-FIND can run between the deletion and the re-creation of an overwritten instance, and find nothing. This is the limit described in the [configuration guide](./PLUGIN_CONFIGURATION.md#standalone-server-or-replica-set). The race does not happen in every run, and it can also hit the next test, `test_raw_frame`, whose `setUp` deletes all patients (404): both are marked `flaky`.
- After the suite, both databases are empty, and the statistics equal a full scan. No attachment, GridFS file, child or tag is orphaned. The only exception is 2 orphaned metadata on the standalone server, which were written after their instance had been deleted.
