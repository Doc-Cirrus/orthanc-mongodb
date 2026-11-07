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
 * Fills an EMPTY database with a synthetic Orthanc index, in the
 * layout that the plugin writes (for instance, the patient tags are
 * copied at the study level, and "StudyDate" is an identifier), with
 * its BSON types (64-bit identifiers), so that the releases before 1.13
 * can read it too. Each instance has the attachment of its DICOM file
 * (512 KB, not stored anywhere), as Orthanc needs it to describe the
 * instance. It is meant for benchmarking the lookups, never for a
 * production server.
 *
 * Usage (the sizes are optional, defaults 20000 x 2 x 3 x 5, i.e.
 * 600,000 instances and 780,000 resources):
 *
 *   mongosh "mongodb://database:27017/bench" --quiet \
 *     --eval "var P = 20000, ST = 2, SE = 3, IN = 5" --file GenerateIndex.js
 *
 * Then point Orthanc at this database, and time the requests with
 * "bench.py". "P = 40000" gives 1,200,000 instances.
 **/

if (db.getCollectionNames().length != 0) {
  throw new Error("The database " + db.getName() + " is not empty");
}

if (typeof P === "undefined") { var P = 20000; }    // Patients
if (typeof ST === "undefined") { var ST = 2; }      // Studies per patient
if (typeof SE === "undefined") { var SE = 3; }      // Series per study
if (typeof IN === "undefined") { var IN = 5; }      // Instances per series
const mods = ["CT", "MR", "US", "CR"];
let id = 0;
let res = [], tags = [], ids = [], files = [];
function flush(force) {
  if (res.length >= 20000 || (force && res.length)) { db.Resources.insertMany(res, {ordered:false}); res = []; }
  if (tags.length >= 20000 || (force && tags.length)) { db.MainDicomTags.insertMany(tags, {ordered:false}); tags = []; }
  if (ids.length >= 20000 || (force && ids.length)) { db.DicomIdentifiers.insertMany(ids, {ordered:false}); ids = []; }
  if (files.length >= 20000 || (force && files.length)) { db.AttachedFiles.insertMany(files, {ordered:false}); files = []; }
}
const L = (x) => NumberLong(String(x));
function pad(n, w) { return String(n).padStart(w, "0"); }
for (let p = 0; p < P; p++) {
  const pid = ++id;
  const pat = {internalId: L(pid), resourceType: 0, publicId: "p" + p, parentId: null, "0": [L(pid)], "1": [], "2": [], "3": []};
  const name = "NAME" + pad(p, 6) + "^X", patientId = "PID" + pad(p, 6);
  tags.push({id: L(pid), tagGroup: 0x10, tagElement: 0x10, value: name});
  tags.push({id: L(pid), tagGroup: 0x10, tagElement: 0x20, value: patientId});
  ids.push({id: L(pid), tagGroup: 0x10, tagElement: 0x20, value: patientId});
  ids.push({id: L(pid), tagGroup: 0x10, tagElement: 0x10, value: name});
  const patDocs = [];
  for (let s = 0; s < ST; s++) {
    const sid = ++id;
    const day = (p * ST + s) % 3650;
    const d = new Date(Date.UTC(2015, 0, 1) + day * 86400000);
    const date = d.getUTCFullYear() + pad(d.getUTCMonth() + 1, 2) + pad(d.getUTCDate(), 2);
    const st = {internalId: L(sid), resourceType: 1, publicId: "s" + p + "_" + s, parentId: L(pid), "0": [L(pid)], "1": [L(sid)], "2": [], "3": [], sorts: [date, "120000"]};
    pat["1"].push(L(sid));
    // Orthanc copies the patient tags at the study level
    for (const [g, e, v] of [[0x10, 0x10, name], [0x10, 0x20, patientId], [8, 0x20, date], [8, 0x30, "120000"], [8, 0x1030, "DESC" + (sid % 100)], [0x20, 0x0d, "1.2." + sid]])
      tags.push({id: L(sid), tagGroup: g, tagElement: e, value: v});
    for (const [g, e, v] of [[0x10, 0x20, patientId], [0x10, 0x10, name], [0x20, 0x0d, "1.2." + sid], [8, 0x1030, "DESC" + (sid % 100)], [8, 0x20, date]])
      ids.push({id: L(sid), tagGroup: g, tagElement: e, value: v});
    for (let e = 0; e < SE; e++) {
      const eid = ++id;
      const se = {internalId: L(eid), resourceType: 2, publicId: "e" + eid, parentId: L(sid), "0": [L(pid)], "1": [L(sid)], "2": [L(eid)], "3": []};
      st["2"].push(L(eid)); pat["2"].push(L(eid));
      tags.push({id: L(eid), tagGroup: 8, tagElement: 0x60, value: mods[(sid + e) % 4]});
      tags.push({id: L(eid), tagGroup: 8, tagElement: 0x103e, value: "SERIES" + e});
      tags.push({id: L(eid), tagGroup: 0x20, tagElement: 0x0e, value: "1.2." + sid + "." + eid});
      ids.push({id: L(eid), tagGroup: 0x20, tagElement: 0x0e, value: "1.2." + sid + "." + eid});
      for (let i = 0; i < IN; i++) {
        const iid = ++id;
        res.push({internalId: L(iid), resourceType: 3, publicId: "i" + iid, parentId: L(eid), "0": [L(pid)], "1": [L(sid)], "2": [L(eid)], "3": [L(iid)], instancePublicId: "i" + iid});
        se["3"].push(L(iid)); st["3"].push(L(iid)); pat["3"].push(L(iid));
        tags.push({id: L(iid), tagGroup: 0x20, tagElement: 0x13, value: String(i + 1)});
        tags.push({id: L(iid), tagGroup: 8, tagElement: 0x18, value: "1.2." + iid});
        ids.push({id: L(iid), tagGroup: 8, tagElement: 0x18, value: "1.2." + iid});
        // The DICOM file (fileType 1), uncompressed (compressionType 1)
        const hash = pad(iid.toString(16), 32);
        files.push({id: L(iid), fileType: 1, uuid: "00000000-0000-4000-8000-" + pad(iid, 12), compressedSize: L(524288),
                    uncompressedSize: L(524288), compressionType: 1, uncompressedHash: hash, compressedHash: hash, revision: L(0)});
      }
      se.instancePublicId = "i" + (eid + 1);
      res.push(se);
    }
    st.instancePublicId = "i" + (sid + 2);
    res.push(st);
  }
  pat.instancePublicId = "i" + (pid + 3);
  res.push(pat);
  flush(false);
}
flush(true);
// The indexes of the lookups, from "MongoDBSchema::CreateIndexes()"
db.Resources.createIndex({parentId: 1}); db.Resources.createIndex({publicId: 1});
db.Resources.createIndex({resourceType: 1}); db.Resources.createIndex({internalId: 1});
for (const c of ["MainDicomTags", "DicomIdentifiers"]) {
  db[c].createIndex({id: 1});
  db[c].createIndex({tagGroup: 1, tagElement: 1, value: 1});
  db[c].createIndex({id: 1, tagGroup: 1, tagElement: 1});
}
db.AttachedFiles.createIndex({id: 1}); db.AttachedFiles.createIndex({uuid: 1});
print(db.Resources.countDocuments(), db.MainDicomTags.countDocuments(), db.DicomIdentifiers.countDocuments(), db.AttachedFiles.countDocuments());
