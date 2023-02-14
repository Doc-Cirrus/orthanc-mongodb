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

**NOTE: Setting up the ConnectionUri overrides the host, port, database params. So if the ConnectionUri is set, the other parameters except the ChunkSize will be ignored.**


