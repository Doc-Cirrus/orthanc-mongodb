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
 * Stop every Orthanc that uses the database before applying. The script
 * refuses to apply while the database looks in use (a live schema
 * upgrade lock, or a change logged in the last 10 minutes); set
 * "repairForce" to true to apply anyway.
 *
 * What is repaired:
 *   - Sequences: the duplicate counters of a sequence are merged into
 *     one, whose value is the largest of the counters and of the
 *     identifiers in use (a counter behind the data is moved forward).
   *   - GlobalProperties, ServerProperties, Metadata, Labels: the document
 *     of each key that the plugin reads (the one "findOne" returns) is
 *     kept. The large values of the removed properties are deleted from
 *     the "LargeProperties" GridFS bucket.
 *   - AttachedFiles: of the attachments of the same resource and type,
 *     the one the plugin reads is kept, unless only another one has its
 *     file in GridFS. A negative statistics change is recorded for each
 *     removed attachment. The UUIDs that no remaining attachment uses
 *     are printed: their files, if any, can be deleted once checked.
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
  const force = (typeof repairForce !== 'undefined') ? repairForce === true : false;

  function format(value) {
    return EJSON.stringify(value, { relaxed: true });
  }

  function collectionExists(name) {
    return db.getCollectionNames().indexOf(name) >= 0;
  }

  // Signs of a running Orthanc
  function findActivity() {
    const signs = [];

    if (collectionExists('Locks') &&
        db.Locks.countDocuments({ expiresAt: { $gt: new Date() } }, { limit: 1 }) > 0) {
      signs.push('a live lock in "Locks"');
    }

    if (collectionExists('Changes')) {
      // Orthanc logs the changes in UTC, as "YYYYMMDDTHHMMSS"
      const pad = function (n) { return String(n).padStart(2, '0'); };
      const t = new Date(Date.now() - 10 * 60 * 1000);
      const since = String(t.getUTCFullYear()) + pad(t.getUTCMonth() + 1) + pad(t.getUTCDate()) + 'T' +
            pad(t.getUTCHours()) + pad(t.getUTCMinutes()) + pad(t.getUTCSeconds());
      if (db.Changes.countDocuments({ date: { $gte: since } }, { limit: 1 }) > 0) {
        signs.push('changes logged in the last 10 minutes');
      }
    }

    return signs;
  }

  if (apply && !force) {
    const signs = findActivity();
    if (signs.length > 0) {
      print('The database looks in use (' + signs.join(', ') + '). Stop every Orthanc first, ' +
            'or set "var repairForce = true" to apply anyway.');
      quit(2);
    }
  }

  // The document of a duplicate group that the plugin reads ("findOne" on the key)
  function readByPlugin(collection, group) {
    return db.getCollection(collection).findOne(group._id);
  }

  // Groups of documents sharing the same key
  function findDuplicates(collection, keys) {
    if (!collectionExists(collection)) {
      return [];
    }

    const groupId = {};
    keys.forEach(function (k) { groupId[k] = '$' + k; });

    return db.getCollection(collection).aggregate([
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
      const top = collectionExists(s.collection) ?
        db.getCollection(s.collection).find({}, { [s.field]: 1 }).sort({ [s.field]: -1 }).limit(1).toArray() : [];
      const maxUsed = (top.length === 0 || top[0][s.field] === undefined) ? 0 : Number(top[0][s.field]);

      if (counters.length === 0) {
        if (maxUsed > 0) {
          actions.push('Sequences: "' + s.sequence + '": create the missing counter at ' + maxUsed);
          if (apply) {
            db.Sequences.updateOne({ name: s.sequence }, { $max: { i: NumberLong(String(maxUsed)) } }, { upsert: true });
          }
        }
        return;
      }
      const value = Math.max(maxUsed, Math.max.apply(null, counters.map(function (c) { return Number(c.i); })));

      if (counters.length > 1 || Number(counters[0].i) !== value) {
        actions.push('Sequences: "' + s.sequence + '": keep one counter (' + counters.length +
                     ' found), set to ' + value + ' (largest identifier in use: ' + maxUsed + ')');

        if (apply) {
          // "$max": an increment by a running Orthanc is never undone
          db.Sequences.updateOne({ _id: counters[0]._id }, { $max: { i: NumberLong(String(value)) } });
          remove('Sequences', counters.slice(1).map(function (c) { return c._id; }));
        }
      }
    });
  }

  // Key-value collections: keep the document the plugin reads
  function deleteLargeValue(document, kept) {
    if (apply && document.valueFile !== undefined &&
        (kept.valueFile === undefined || String(kept.valueFile) !== String(document.valueFile))) {
      db.getCollection('LargeProperties.chunks').deleteMany({ files_id: document.valueFile });
      db.getCollection('LargeProperties.files').deleteOne({ _id: document.valueFile });
    }
  }

  function describeValue(document) {
    return document.valueFile !== undefined ? '(large value in LargeProperties)' : format(document.value);
  }

  [
    { collection: 'GlobalProperties', keys: ['property'] },
    { collection: 'ServerProperties', keys: ['server', 'property'] },
    { collection: 'Metadata',         keys: ['id', 'type'] },
    { collection: 'Labels',           keys: ['id', 'label'] },
  ].forEach(function (c) {
    findDuplicates(c.collection, c.keys).forEach(function (group) {
      const kept = readByPlugin(c.collection, group);
      const removed = group.documents.filter(function (d) { return String(d._id) !== String(kept._id); });
      actions.push(c.collection + ' ' + format(group._id) + ': keep ' +
                   (c.collection === 'Labels' ? 'one' : describeValue(kept)) + ', remove ' + removed.length);
      removed.forEach(function (d) { deleteLargeValue(d, kept); });
      remove(c.collection, removed.map(function (d) { return d._id; }));
    });
  });

  // Attachments: keep the one the plugin reads, unless only another one has its file
  const gridfs = collectionExists('fs.files');

  findDuplicates('AttachedFiles', ['id', 'fileType']).forEach(function (group) {
    const hasFile = function (d) {
      return db.getCollection('fs.files').countDocuments({ filename: d.uuid + ' - ' + d.fileType }, { limit: 1 }) > 0;
    };

    let kept = readByPlugin('AttachedFiles', group);
    if (gridfs && !hasFile(kept)) {
      kept = group.documents.find(hasFile) || kept;
    }

    const removed = group.documents.filter(function (d) { return String(d._id) !== String(kept._id); });

    // The UUIDs still used by the kept attachment (or by any other) keep their files
    const removedIds = removed.map(function (d) { return d._id; });
    const orphans = removed.map(function (d) { return d.uuid; }).filter(function (uuid, i, all) {
      return all.indexOf(uuid) === i &&
        db.AttachedFiles.countDocuments({ uuid: uuid, _id: { $nin: removedIds } }, { limit: 1 }) === 0;
    });

    actions.push('AttachedFiles ' + format(group._id) + ': keep ' + kept.uuid + ', remove ' + removed.length +
                 (orphans.length > 0 ?
                  '; files no attachment uses any more' + (gridfs ? ' (left in GridFS)' : '') + ': ' + orphans.join(', ') :
                  ''));

    if (apply && removed.length > 0) {
      remove('AttachedFiles', removedIds);

      // As the plugin does when it deletes an attachment
      db.StatisticsChanges.insertMany(removed.map(function (d) {
        return { compressedSize: NumberLong(String(-Number(d.compressedSize))),
                 uncompressedSize: NumberLong(String(-Number(d.uncompressedSize))) };
      }));
    }
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
