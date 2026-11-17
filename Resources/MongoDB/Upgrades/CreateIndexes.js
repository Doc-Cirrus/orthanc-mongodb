/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

/**
 * Offline build of the indexes of schema revision 2, ahead of an
 * upgrade of the plugin. On millions of documents, building them here,
 * outside of peak hours, keeps the first start of the new plugin short
 * (it then only checks that each index exists), and lets a deployment
 * set "CreateIndexesAtStartup" to false.
 *
 * The list, the names and the options are the ones of
 * "MongoDB/Plugins/MongoDBSchema.cpp", which must be kept in sync.
 *
 * A unique index is not built if the data has duplicates: the script
 * reports it, and goes on with the other indexes. Run
 * "AuditDuplicates.js" to list the duplicates, and "RepairDuplicates.js"
 * to repair them.
 *
 * The script never changes the "DatabasePatchLevel" global property:
 * the next start of the plugin sets the revision once every index exists.
 *
 * Usage:
 *   mongosh "mongodb://host:27017/orthanc" --quiet --file CreateIndexes.js
 *
 * Exit code: 0 if every index exists at the end, 1 otherwise.
 **/

(function () {
  'use strict';

  // [collection, keys, unique]
  const indexes = [
    // Revision 1 (the previous versions of the plugin)
    ['fs.files',              { filename: 1 }],
    ['Resources',             { parentId: 1 }],
    ['Resources',             { publicId: 1 }],
    ['Resources',             { resourceType: 1 }],
    ['Resources',             { internalId: 1 }],
    ['PatientRecyclingOrder', { patientId: 1 }],
    ['MainDicomTags',         { id: 1 }],
    ['MainDicomTags',         { tagGroup: 1, tagElement: 1, value: 1 }],
    ['DicomIdentifiers',      { id: 1 }],
    ['DicomIdentifiers',      { tagGroup: 1, tagElement: 1, value: 1 }],
    ['Changes',               { internalId: 1 }],
    ['AttachedFiles',         { id: 1 }],
    ['Metadata',              { id: 1 }],
    ['GlobalProperties',      { property: 1 }],
    ['ServerProperties',      { server: 1, property: 1 }],

    // Revision 2: the level arrays, for "AttachChild()" and "LookupResources()"
    ['Resources',             { '0': 1, resourceType: 1 }],
    ['Resources',             { '1': 1, resourceType: 1 }],
    ['Resources',             { '2': 1, resourceType: 1 }],
    ['Resources',             { '3': 1, resourceType: 1 }],

    // Revision 2: the correlated "$lookup" of "LookupResources()"
    ['MainDicomTags',         { id: 1, tagGroup: 1, tagElement: 1 }],
    ['DicomIdentifiers',      { id: 1, tagGroup: 1, tagElement: 1 }],

    // Revision 2: the pages of "ExecuteFind()" without ordering
    ['Resources',             { resourceType: 1, publicId: 1, internalId: 1 }],

    // Revision 2: the sequence fields, and the attachments by UUID
    ['Changes',               { id: 1 }],
    ['ExportedResources',     { id: 1 }],
    ['PatientRecyclingOrder', { id: 1 }],
    ['AttachedFiles',         { uuid: 1 }],

    // Revision 2: the unique indexes
    ['Resources',             { publicId: 1 },             true],
    ['Resources',             { internalId: 1 },           true],
    ['Sequences',             { name: 1 },                 true],
    ['GlobalProperties',      { property: 1 },             true],
    ['ServerProperties',      { server: 1, property: 1 },  true],
    ['AttachedFiles',         { id: 1, fileType: 1 },      true],
    ['Metadata',              { id: 1, type: 1 },          true],

    // Revision 2: the labels, by resource (unique) and by name
    ['Labels',                { id: 1, label: 1 },         true],
    ['Labels',                { label: 1, id: 1 }],

    // Revision 2: the key-value stores (unique) and the queues, in their order
    ['KeyValueStores',        { storeId: 1, key: 1 },      true],
    ['Queues',                { queueId: 1, id: 1 }],

    // Revision 2: the audit logs, in chronological order
    ['AuditLogs',             { ts: 1 }],
    ['AuditLogs',             { userId: 1, ts: 1 }],
    ['AuditLogs',             { resourceId: 1, ts: 1 }],
    ['AuditLogs',             { action: 1, ts: 1 }],
  ];

  // Same names as "IndexDefinition::GetName()" in "MongoDBSchema.cpp"
  function getName(keys, unique) {
    const name = Object.keys(keys).map(function (k) { return k + '_1'; }).join('_');
    return unique ? name + '_unique' : name;
  }

  function sameKeys(a, b) {
    const ka = Object.keys(a);
    const kb = Object.keys(b);
    return (ka.length === kb.length &&
            ka.every(function (k, i) { return k === kb[i] && Number(a[k]) === 1; }));
  }

  function exists(collection, keys, unique) {
    if (db.getCollectionNames().indexOf(collection) < 0) {
      return false;
    }

    return db.getCollection(collection).getIndexes().some(function (index) {
      return sameKeys(index.key, keys) && (!unique || index.unique === true);
    });
  }

  let missing = 0;

  indexes.forEach(function (definition) {
    const collection = definition[0];
    const keys = definition[1];
    const unique = (definition[2] === true);
    const name = collection + '.' + getName(keys, unique);

    if (exists(collection, keys, unique)) {
      print('exists:  ' + name);
      return;
    }

    const options = { name: getName(keys, unique) };
    if (unique) {
      options.unique = true;
    }

    const started = Date.now();
    print('build:   ' + name + ' (about ' + db.getCollection(collection).estimatedDocumentCount() + ' documents)');

    try {
      db.getCollection(collection).createIndex(keys, options);
      print('built:   ' + name + ' (' + (Date.now() - started) / 1000 + 's)');
    } catch (e) {
      if (unique && e.code === 11000) {
        print('SKIPPED: ' + name + ': the data has duplicates, see AuditDuplicates.js');
        missing++;
      } else {
        throw e;
      }
    }
  });

  if (missing > 0) {
    print('\n' + missing + ' unique index(es) not built: the plugin keeps the schema revision 1 until they exist.');
    quit(1);
  } else {
    print('\nEvery index of schema revision 2 exists.');
  }
})();
