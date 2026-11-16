# Plugin Configuration

In order to use the plugin you need to include it in the `configuration.json` file of orthanc:

```json
...
"Plugins": [
    "path/to/plugin/libOrthancMongoDBIndex.so",
    "path/to/plugin/libOrthancMongoDBStorage.so"
]
...
```

ANd to configure the plugin itself 

```json
...
// MongoDB plugin confihuration section:
"MongoDB" : {
    "EnableIndex" : true, // false to use default SQLite 
    "EnableStorage" : true, // false to use default SQLite 
    "ConnectionUri" : "mongodb://localhost:27017/orthanc_db",
    "ChunkSize" : 261120
},
...
```

Also it's possible to configure the plugin with separate config options:

```json
...
"MongoDB" : {
    "host" : "customhost",
    "port" : 27001,
    "user" : "user",
    "database" : "database",
    "password" : "password",
    "authenticationDatabase" : "admin",
    "ChunkSize" : 261120
}
...
```

### Maximum duration of a search

`"MaxQueryTime"` (in seconds, `0` by default, i.e. no limit) makes the MongoDB server stop a
query of a search that runs longer. The limit applies to the searches only (the Find and Count
of Orthanc, and the legacy lookups, with their probes and the reads of the children), never to the
stores and the deletes: it must not cut a sequence of writes. It applies to each query, not to a
whole request: one Find of Orthanc runs several queries (the page, a possible rerun, the probes,
the children), so it can last several times `MaxQueryTime`. The request then fails
with the Orthanc error `Timeout` and is not retried, which frees the connection and the thread
that were held, e.g. when an Orthanc Explorer 2 user retypes a search while the previous one is
still running.

```json
"MongoDB" : {
    "ConnectionUri" : "mongodb://localhost:27017/orthanc_db",
    "MaxQueryTime" : 60
}
```

**NOTE: Setting up the ConnectionUri overrides the host, port, database params. So if the ConnectionUri is set, the other parameters except the ChunkSize will be ignored.**



## Integrated Find (`EnableExtendedFind`)

By default (`"EnableExtendedFind": true`), the index plugin answers the Find and Count requests of Orthanc (`/tools/find`, `/tools/count-resources`, the paged lists and the study list of Orthanc Explorer 2) with MongoDB aggregations, instead of the generic Find of Orthanc, which needs one round trip per resource. With `false`, Orthanc uses its generic Find, which cannot sort (the requests with `OrderBy` fail).

A page (`Limit`) of the resources of a level, without ordering or ordered by a string tag of this level, is read from the indexes in time that depends on the size of the page. These requests are **known slow paths**: they fall back to one lookup per candidate resource followed by a sort, so their time grows with the number of resources that match the filters:

- an ordering by a metadata, with no other filter;
- an ordering cast to an integer or a float (`ORDERING_CAST_INT`, `ORDERING_CAST_FLOAT`);
- an ordering by a tag of an ancestor level (e.g. series ordered by `StudyDate`).
- an ordering by several keys when many resources share the value of the first key (e.g. an empty `StudyDescription`): all of them are joined and sorted.

A label filter (`ALL`, `ANY`) starts from the labels if it matches fewer than 1,000 labels, and otherwise from the ordering index with the labels as a filter, so a label that tags a large share of the studies is not a slow path. Only a request that cannot use the ordering index (the cases above) joins all the labelled resources.

The children of the resources of a page (identifiers, tags, metadata of their instances) are read from the children themselves, not gathered into one document per resource, so they are not limited by the 16 MB of a MongoDB document, whatever the number of instances.

The indexes that these requests use are created at startup (`CreateIndexesAtStartup`), or offline with `Resources/MongoDB/Upgrades/CreateIndexes.js`. `Resources/MongoDB/Benchmark` times typical requests.

## Standalone server or replica set

On a replica set, a request runs in a transaction and sees one consistent snapshot. On a standalone server there are no transactions, so the integrated Find reads in several steps that see different instants: the page of resources, then their children. A child deleted in between gives a resource with fewer (or no) children, and an instance stored in between appears among the children although the resource itself predates it. Like the other transient states of a standalone server (e.g. a resource being created or deleted), this disappears with the next request. A count-only request of the children reads the level array of the resource, which a delete interrupted on a standalone server can leave listing a deleted child until it is repaired (the identifiers do not have this difference). When the content of one instance is requested, a resource whose instance cannot be found (e.g. a study without instances) is left out of the page, so the page can have one row less than `Limit` while the resource is counted: do not stop at the first page shorter than `Limit`. Use a replica set if this matters.
