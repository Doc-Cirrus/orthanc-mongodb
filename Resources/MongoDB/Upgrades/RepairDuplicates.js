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
 * Repairs the duplicates that "AuditDuplicates.js" reports, so that the
 * unique indexes of schema revision 2 can be built. The plugin never
 * runs this script: someone must run it explicitly, after a backup.
 *
 * By default the script only prints what it would do. Set
 * "repairApply" to true to change the database:
 *
 *   mongosh "mongodb://host:27017/orthanc" --quiet --file RepairDuplicates.js
 *   mongosh "mongodb://host:27017/orthanc" --quiet --eval "var repairApply = true" --file RepairDuplicates.js
 *
 * Stop every Orthanc that uses the database before applying.
 *
 * What is repaired:
 *   - Sequences: the duplicate counters of a sequence are merged into
 *     one, whose value is the largest of the counters and of the
 *     identifiers in use (a counter behind the data is moved forward).
 *   - GlobalProperties, ServerProperties, Metadata: the most recently
 *     inserted document of each key is kept (the largest "_id").
 *   - AttachedFiles: of the attachments of the same resource and type,
 *     the most recent one whose file is in GridFS is kept. The UUIDs of
 *     the removed attachments are printed: their files, if any, are
 *     left in GridFS, and can be deleted once checked.
 *
 * What is only reported:
 *   - Resources with the same "publicId" or "internalId". Their children,
 *     tags, attachments and changes may refer to either copy, so they
 *     cannot be merged safely by a script. The plugin keeps working on
 *     such a database, at schema revision 1.
 *
 * Exit code: 0 if nothing is left to repair by hand, 1 otherwise.
 **/

(function () {
  'use strict';

  const apply = (typeof repairApply !== 'undefined') ? repairApply === true : false;

  function format(value) {
    return EJSON.stringify(value, { relaxed: true });
  }

  function collectionExists(name) {
    return db.getCollectionNames().indexOf(name) >= 0;
  }

  // Groups of documents sharing the same key, the largest "_id" first
  function findDuplicates(collection, keys) {
    if (!collectionExists(collection)) {
      return [];
    }

    const groupId = {};
    keys.forEach(function (k) { groupId[k] = '$' + k; });

    return db.getCollection(collection).aggregate([
      { $sort: { _id: -1 } },
      { $group: { _id: groupId, documents: { $push: '$$ROOT' }, count: { $sum: 1 } } },
      { $match: { count: { $gt: 1 } } },
    ], { allowDiskUse: true }).toArray();
  }

  function remove(collection, ids) {
    if (apply && ids.length > 0) {
      db.getCollection(collection).deleteMany({ _id: { $in: ids } });
    }
  }

  const actions = [];
  let manual = 0;

  // Sequences
  const sequences = [
    { sequence: 'Resources',             collection: 'Resources',             field: 'internalId' },
    { sequence: 'Changes',               collection: 'Changes',               field: 'id' },
    { sequence: 'ExportedResources',     collection: 'ExportedResources',     field: 'id' },
    { sequence: 'PatientRecyclingOrder', collection: 'PatientRecyclingOrder', field: 'id' },
  ];

  if (collectionExists('Sequences')) {
    sequences.forEach(function (s) {
      const counters = db.Sequences.find({ name: s.sequence }).sort({ _id: -1 }).toArray();
      if (counters.length === 0) {
        return;
      }

      const top = collectionExists(s.collection) ?
        db.getCollection(s.collection).find({}, { [s.field]: 1 }).sort({ [s.field]: -1 }).limit(1).toArray() : [];
      const maxUsed = (top.length === 0 || top[0][s.field] === undefined) ? 0 : Number(top[0][s.field]);
      const value = Math.max(maxUsed, Math.max.apply(null, counters.map(function (c) { return Number(c.i); })));

      if (counters.length > 1 || Number(counters[0].i) !== value) {
        actions.push('Sequences: "' + s.sequence + '": keep one counter (' + counters.length +
                     ' found), set to ' + value + ' (largest identifier in use: ' + maxUsed + ')');

        if (apply) {
          db.Sequences.updateOne({ _id: counters[0]._id }, { $set: { i: NumberLong(String(value)) } });
          remove('Sequences', counters.slice(1).map(function (c) { return c._id; }));
        }
      }
    });
  }

  // Key-value collections: keep the most recent document
  [
    { collection: 'GlobalProperties', keys: ['property'] },
    { collection: 'ServerProperties', keys: ['server', 'property'] },
    { collection: 'Metadata',         keys: ['id', 'type'] },
  ].forEach(function (c) {
    findDuplicates(c.collection, c.keys).forEach(function (group) {
      const removed = group.documents.slice(1);
      actions.push(c.collection + ' ' + format(group._id) + ': keep ' +
                   format(group.documents[0].value) + ', remove ' + removed.length);
      remove(c.collection, removed.map(function (d) { return d._id; }));
    });
  });

  // Attachments: keep the most recent one whose file exists
  findDuplicates('AttachedFiles', ['id', 'fileType']).forEach(function (group) {
    const hasFile = function (d) {
      return db.getCollection('fs.files').countDocuments({ filename: d.uuid + ' - ' + d.fileType }, { limit: 1 }) > 0;
    };

    const kept = group.documents.find(hasFile) || group.documents[0];
    const removed = group.documents.filter(function (d) { return d._id !== kept._id; });

    actions.push('AttachedFiles ' + format(group._id) + ': keep ' + kept.uuid +
                 ', remove ' + removed.map(function (d) { return d.uuid; }).join(', ') +
                 ' (their files stay in GridFS)');
    remove('AttachedFiles', removed.map(function (d) { return d._id; }));
  });

  // Resources: report only
  ['publicId', 'internalId'].forEach(function (key) {
    findDuplicates('Resources', [key]).forEach(function (group) {
      manual++;
      actions.push('MANUAL: Resources with ' + key + ' ' + format(group._id[key]) + ': ' + group.count +
                   ' documents: ' + group.documents.map(function (d) {
                     return d.publicId + ' (internalId ' + String(d.internalId) + ')';
                   }).join(', '));
    });
  });

  actions.forEach(function (a) { print((apply || a.indexOf('MANUAL') === 0 ? '' : 'would: ') + a); });

  if (actions.length === 0) {
    print('Nothing to repair.');
  } else if (!apply && actions.length > manual) {
    print('\nNothing was changed. Run again with --eval "var repairApply = true" to apply.');
  }

  if (manual > 0) {
    print('\n' + manual + ' duplicate resource(s) must be repaired by hand. ' +
          'Until then, the unique indexes of Resources are not built.');
    quit(1);
  }
})();
