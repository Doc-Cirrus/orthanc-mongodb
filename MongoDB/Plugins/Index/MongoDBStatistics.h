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


#pragma once

#include "../../../Framework/Common/DatabaseManager.h"
#include "../../../Framework/MongoDB/MongoDBDatabase.h"


namespace OrthancDatabases
{
  /**
   * The number of resources of each level, and the total size of the
   * attachments, as kept by "UpdateAndGetStatistics()".
   **/
  struct MongoDBStatisticsValues
  {
    int64_t  counts_[4];         // Indexed by "OrthancPluginResourceType"
    int64_t  compressedSize_;
    int64_t  uncompressedSize_;

    MongoDBStatisticsValues();

    bool IsZero() const;

    MongoDBStatisticsValues& operator+= (const MongoDBStatisticsValues& other);

    MongoDBStatisticsValues& operator-= (const MongoDBStatisticsValues& other);

    // One field per non-zero value, e.g. {"instances": 1, "compressedSize": 1234}
    bsoncxx::document::value ToDocument() const;

    void FromDocument(const bsoncxx::document::view& document);  // Missing fields are 0
  };


  /**
   * Counterpart of the "GlobalIntegers" and "GlobalIntegersChanges"
   * tables of the PostgreSQL plugin (bug 10 of "PLAN.md"):
   *
   * - Each write that changes a count or a size inserts its change
   *   into the append-only collection "StatisticsChanges", in the
   *   active transaction if any. Concurrent writers therefore never
   *   update the same document, and never conflict on a replica set.
   *
   * - The totals are one document of "GlobalProperties" (the property
   *   "DatabaseInternal0"). The current values are the totals plus the
   *   pending changes. The housekeeping and "UpdateAndGetStatistics()"
   *   fold the pending changes into the totals.
   *
   * - A database of the previous versions has no totals: they are
   *   computed once by a full scan, in the housekeeping. Until then,
   *   every read is a full scan, as in the previous versions.
   *
   * Without a transaction (standalone server), an interruption in the
   * middle of a write or of a consolidation can make the statistics
   * drift. Deleting the totals document makes the housekeeping compute
   * them again. The previous versions of the plugin record no changes,
   * so the statistics are only exact if every Orthanc on the database
   * runs this version.
   **/
  class MongoDBStatistics : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

    bool LookupTotals(MongoDBStatisticsValues& totals);

    void SumChanges(MongoDBStatisticsValues& target,
                    const bsoncxx::document::view_or_value& filter);

    // The values computed from the data, by a full scan
    static void Scan(MongoDBStatisticsValues& target,
                     mongocxx::database& database,
                     const mongocxx::client_session* session);

  public:
    explicit MongoDBStatistics(DatabaseManager& manager);

    explicit MongoDBStatistics(MongoDBDatabase& database);

    void RecordChange(const MongoDBStatisticsValues& change);

    // The totals plus the pending changes, or a full scan if the totals are not computed yet
    void GetValues(MongoDBStatisticsValues& target);

    // Folds the pending changes into the totals. Returns false if the totals are not computed yet.
    bool Consolidate();

    /**
     * Computes the totals if they are missing, with a full scan. On a
     * replica set, the scan and the pending changes are read from the
     * same snapshot, so the totals are exact. Without snapshots, the
     * totals are only stored if no change was recorded during the scan,
     * unless "force" is true. Returns false if the totals are missing.
     **/
    bool ComputeTotals(bool force);

    // If the database has no resource and no attachment, stores zero totals
    void InitializeIfEmpty();
  };
}
