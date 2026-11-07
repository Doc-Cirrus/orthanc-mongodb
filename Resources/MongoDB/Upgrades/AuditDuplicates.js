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
 * Pre-flight audit of an Orthanc MongoDB index before the unique indexes
 * of schema revision 2 are created.
 *
 * This script is READ-ONLY: it never modifies the database. It reports:
 *   - duplicate keys that would prevent a unique index from being built;
 *   - sequence counters that are behind the largest identifier in use
 *     (the next identifier handed out would collide with an existing one).
 *
 * Usage:
 *   mongosh "mongodb://host:27017/orthanc" --quiet --file AuditDuplicates.js
 *
 * Optional variables (set them with --eval before --file):
 *   auditSampleSize  - number of example keys printed per duplicate group (default 5)
 *   auditSecondary   - true to read from a secondary if available (default false)
 *
 *   mongosh "mongodb://host/orthanc" --eval "var auditSecondary = true" --file AuditDuplicates.js
 *
 * Each check scans a whole collection. On databases with millions of
 * documents, run it against a copy or a secondary, outside of peak hours.
 *
 * Exit code: 0 if no problem was found, 1 otherwise.
 **/

(function () {
  'use strict';

  const sampleSize = (typeof auditSampleSize !== 'undefined') ? auditSampleSize : 5;
  const useSecondary = (typeof auditSecondary !== 'undefined') ? auditSecondary : false;

  if (useSecondary) {
    db.getMongo().setReadPref('secondaryPreferred');
  }

  // The unique indexes that schema revision 2 wants to create
  const uniqueChecks = [
    { collection: 'Resources',        keys: ['publicId'] },
    { collection: 'Resources',        keys: ['internalId'] },
    { collection: 'Sequences',        keys: ['name'] },
    { collection: 'GlobalProperties', keys: ['property'] },
    { collection: 'ServerProperties', keys: ['server', 'property'] },
    { collection: 'AttachedFiles',    keys: ['id', 'fileType'] },
    { collection: 'Metadata',         keys: ['id', 'type'] },
  ];

  // Sequence counters and the field whose maximum they must not be behind
  const sequenceChecks = [
    { sequence: 'Resources',             collection: 'Resources',             field: 'internalId' },
    { sequence: 'Changes',               collection: 'Changes',               field: 'id' },
    { sequence: 'ExportedResources',     collection: 'ExportedResources',     field: 'id' },
    { sequence: 'PatientRecyclingOrder', collection: 'PatientRecyclingOrder', field: 'id' },
  ];

  function collectionExists(name) {
    return db.getCollectionNames().indexOf(name) >= 0;
  }

  function findDuplicates(check) {
    const groupId = {};
    check.keys.forEach(function (k) { groupId[k] = '$' + k; });

    const pipeline = [
      { $group: { _id: groupId, count: { $sum: 1 } } },
      { $match: { count: { $gt: 1 } } },
      { $group: {
          _id: null,
          groups: { $sum: 1 },
          extraDocuments: { $sum: { $subtract: ['$count', 1] } },
          samples: { $push: { key: '$_id', count: '$count' } } } },
      { $project: { _id: 0, groups: 1, extraDocuments: 1, samples: { $slice: ['$samples', sampleSize] } } },
    ];

    const result = db.getCollection(check.collection).aggregate(pipeline, { allowDiskUse: true }).toArray();
    return result.length === 0 ? { groups: 0, extraDocuments: 0, samples: [] } : result[0];
  }

  function checkSequence(check) {
    const counters = db.getCollection('Sequences').find({ name: check.sequence }).toArray();
    const top = collectionExists(check.collection) ?
      db.getCollection(check.collection).find({}, { [check.field]: 1 }).sort({ [check.field]: -1 }).limit(1).toArray() : [];
    const maxUsed = (top.length === 0 || top[0][check.field] === undefined) ? 0 : Number(top[0][check.field]);
    const counterValues = counters.map(function (c) { return Number(c.i); });
    const effective = counterValues.length === 0 ? 0 : Math.max.apply(null, counterValues);

    return {
      sequence: check.sequence,
      counterDocuments: counters.length,
      counterValues: counterValues,
      maxUsed: maxUsed,
      behind: effective < maxUsed,
    };
  }

  const report = {
    database: db.getName(),
    date: new Date().toISOString(),
    uniqueIndexes: [],
    sequences: [],
    problems: 0,
  };

  uniqueChecks.forEach(function (check) {
    const name = check.collection + '(' + check.keys.join(', ') + ')';

    if (!collectionExists(check.collection)) {
      report.uniqueIndexes.push({ index: name, status: 'collection missing' });
      return;
    }

    const started = Date.now();
    const dup = findDuplicates(check);
    const entry = {
      index: name,
      status: dup.groups === 0 ? 'ok' : 'DUPLICATES',
      duplicateKeys: dup.groups,
      extraDocuments: dup.extraDocuments,
      seconds: (Date.now() - started) / 1000,
    };

    if (dup.groups > 0) {
      entry.samples = dup.samples;
      report.problems++;
    }

    report.uniqueIndexes.push(entry);
  });

  if (collectionExists('Sequences')) {
    sequenceChecks.forEach(function (check) {
      const entry = checkSequence(check);
      entry.status = 'ok';

      if (entry.counterDocuments > 1) {
        entry.status = 'DUPLICATE COUNTERS';
        report.problems++;
      } else if (entry.behind) {
        entry.status = 'COUNTER BEHIND DATA';
        report.problems++;
      }

      report.sequences.push(entry);
    });
  }

  printjson(report);

  if (report.problems > 0) {
    print('\n' + report.problems + ' problem(s) found. The plugin will not create the affected ' +
          'unique indexes and keeps working as before. See RepairDuplicates.js to fix the data.');
    quit(1);
  } else {
    print('\nNo problem found: the unique indexes of schema revision 2 can be created.');
  }
})();
