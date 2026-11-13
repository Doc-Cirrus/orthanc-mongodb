# Plugin Configuration

## Loading the plugins

The repository builds two plugins, which can be used together or separately:

- `libOrthancMongoDBIndex.so` replaces the SQLite index of Orthanc (patients, studies, series, instances, tags, metadata, changes...).
- `libOrthancMongoDBStorage.so` stores the DICOM files and the other attachments in GridFS, instead of the filesystem.

```json
"Plugins" : [
  "/usr/local/share/orthanc/plugins/libOrthancMongoDBIndex.so",
  "/usr/local/share/orthanc/plugins/libOrthancMongoDBStorage.so"
]
```

Requirements:

- Orthanc **1.13.0** or later. The index plugin only uses the V4 database API.
- MongoDB server **7.0** or later. The plugins refuse to start on older servers.

## The "MongoDB" section

```json
"MongoDB" : {
  "EnableIndex" : true,
  "EnableStorage" : true,
  "ConnectionUri" : "mongodb://localhost:27017/orthanc"
}
```

The connection URI must contain the name of the database (`/orthanc` above). The user, the password, the authentication database, TLS and the replica set are all set in the URI, with the [standard MongoDB syntax](https://www.mongodb.com/docs/manual/reference/connection-string/), for example:

```
mongodb://orthanc:secret@db1:27017,db2:27017,db3:27017/orthanc?replicaSet=rs0&authSource=admin
```

With the [release binaries](../README.md#supported-platforms), TLS uses the certificate authorities of the system (`/etc/ssl/cert.pem` or `/etc/ssl/certs`); on a system that keeps them elsewhere, add `tlsCAFile=<file>` to the URI, or set the environment variable `SSL_CERT_FILE` of Orthanc. These binaries do not support Kerberos (`authMechanism=GSSAPI`).

### Connection as separate options

Instead of `ConnectionUri`, the connection can be given as separate options, as in the releases up to 1.9.1:

```json
"MongoDB" : {
  "EnableIndex" : true,
  "EnableStorage" : true,
  "host" : "customhost",
  "port" : 27001,
  "user" : "user",
  "database" : "database",
  "password" : "password",
  "authenticationDatabase" : "admin",
  "ChunkSize" : 261120
}
```

The plugins build the connection URI from them: `mongodb://user:password@customhost:27001/database?authSource=admin` above.

| Option | Default | In the URI |
|---|---|---|
| `host` | `localhost` | One host name or address. An IPv6 address, e.g. `::1`, is put in brackets. |
| `port` | `27017` | The port, from 1 to 65535. |
| `database` | (required) | The name of the database. |
| `user` | none | The user. Without it, the plugins connect without credentials. |
| `password` | none | The password of `user`. |
| `authenticationDatabase` | none | `authSource`: the database where the user is defined, e.g. `admin`. |

- `ConnectionUri` takes precedence: if it is set (and not empty), these options are ignored, and the plugins log a warning that names them.
- The user, the password and the authentication database are percent-encoded, so they can contain any character. (The releases up to 1.9.1 did not encode them.) The password never appears in the logs, nor in the error messages.
- An empty value counts as absent. This allows environment variables that may not be set, which Orthanc replaces when it reads the file, e.g. `"password" : "${MONGODB_PASSWORD}"`. This also keeps the password out of the configuration file.
- These options describe one host. For several hosts, a replica set name, TLS or any other option of the URI, use `ConnectionUri`. A single-node replica set given by its host is still detected, and its transactions are used.

| Option | Default | Description |
|---|---|---|
| `EnableIndex` | `false` | Use MongoDB for the index. |
| `EnableStorage` | `false` | Use GridFS for the attachments. |
| `ConnectionUri` | (required, unless `database` is set) | MongoDB connection string, with the name of the database. Or use the [separate options](#connection-as-separate-options). |
| `ChunkSize` | `261120` | Size, in bytes, of the GridFS chunks of new files. |
| `IndexConnectionsCount` | `5` | Number of connections of the index to MongoDB. Each connection runs one Orthanc transaction at a time. |
| `UseDynamicConnectionPool` | `false` | Open the index connections on demand, up to `IndexConnectionsCount`, instead of all of them at startup. |
| `MaximumConnectionRetries` | `10` | Number of attempts to connect at startup. `MaxConnectionRetries`, the name used by the previous releases, still works. |
| `ConnectionRetryInterval` | `5` | Seconds between two connection attempts at startup. |
| `EnableTransactions` | `"Auto"` | `"Auto"`: use multi-document transactions if the server supports them (replica set or sharded cluster). `true`: require them, so the plugin refuses to start on a standalone server. `false`: never use them. |
| `CreateIndexesAtStartup` | `true` | Build the missing indexes at startup. With `false`, the plugin only checks them, and logs those to build offline (see below). |
| `EnableExtendedFind` | `true` | Answer `/tools/find` and C-FIND with the plugin's own pipeline (`ExecuteFind`). With `false`, Orthanc uses its generic implementation. |
| `HousekeepingInterval` | `1` | Seconds between two runs of the database housekeeping (consolidation of the statistics), once Orthanc has started. |
| `AuditLogsRetentionDays` | `0` | Days after which MongoDB deletes the audit logs (TTL index). `0` keeps them forever. |

The top-level Orthanc option `"ReadOnly" : true` is honored: the index plugin then never writes, nor upgrades the database.

The audit logs, written by the plugins that use the audit-log API of the SDK 1.12.9, can be read with `GET /plugins/mongodb/audit-logs`.

## Standalone server or replica set

The plugins work with both, but they do not give the same guarantees when several writers run at the same time (the index connections of one Orthanc, or several Orthanc on the same database):

- **Replica set** (recommended, even with a single node): each Orthanc transaction is one MongoDB transaction. Concurrent stores, deletions and lookups behave as with PostgreSQL.
- **Standalone server**: MongoDB has no multi-document transactions, so an Orthanc transaction is a sequence of single-document writes. The plugin keeps the data consistent: resources and counters are created by upserts protected by unique indexes, and a store that races the deletion of its patient is either retried or failed, without losing or leaking a file. But other requests can see the intermediate states:
  - with `"OverwriteInstances" : true`, a lookup that runs while an instance is overwritten may not find it;
  - an Orthanc transaction that is retried after a conflict cannot undo its first writes; for example, the file of an overwritten instance can stay in GridFS.

A single-node replica set is started with `mongod --replSet rs0`, then `rs.initiate()` in mongosh, and used with `?replicaSet=rs0` in the URI. `.docker/docker-compose.yml` has an example (`database-rs`).

## Upgrading from a previous release

The existing collections and documents are kept as they are. Schema revision 2 only adds indexes (7 of them unique) and new collections. At the first start, the plugin builds the missing indexes under a lock, so that only one Orthanc upgrades the database. On millions of documents this can take a long time, so on a large database:

1. Back up the database.
2. Check that no duplicate blocks a unique index, and repair them if needed:
   ```bash
   mongosh "mongodb://host:27017/orthanc" --quiet --file Resources/MongoDB/Upgrades/AuditDuplicates.js
   mongosh "mongodb://host:27017/orthanc" --quiet --file Resources/MongoDB/Upgrades/RepairDuplicates.js   # dry run
   mongosh "mongodb://host:27017/orthanc" --quiet --eval "var repairApply = true" --file Resources/MongoDB/Upgrades/RepairDuplicates.js
   ```
3. Build the indexes offline, outside of peak hours, while the previous release keeps running:
   ```bash
   mongosh "mongodb://host:27017/orthanc" --quiet --file Resources/MongoDB/Upgrades/CreateIndexes.js
   ```
4. Start the new release. It finds every index, and only records the new revision. `CreateIndexesAtStartup` can then be `false`.

If a unique index cannot be built because of duplicates, the plugin still runs, logs it, and the database stays at revision 1 until the index exists.

**Downgrade:** the previous release still runs on an upgraded database, as it ignores the new indexes and collections. The data written by the new features (labels, key-value stores, queues, audit logs) is ignored by the previous release.

