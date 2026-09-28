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

#include "../../Framework/Common/DatabaseManager.h"
#include "../../Framework/MongoDB/MongoDBParameters.h"

#include <list>
#include <string>


namespace OrthancDatabases
{
  /**
   * Counterpart of the schema handling in "PostgreSQLIndex": the
   * indexes of the collections, and the version of the database. The
   * shape of the documents never changes: a new revision only adds
   * indexes, collections or optional fields, so that the previous
   * versions of the plugin still run against an upgraded database.
   *
   * The revision is the "DatabasePatchLevel" global property:
   *
   *   1  The layout of the previous versions of the plugin (which
   *      reset the property to 1 whenever they start).
   *   2  The unique indexes, and the indexes of the lookups, of the
   *      sequences, and of the new "Labels", "KeyValueStores",
   *      "Queues" and "AuditLogs" collections. The revision stays at 1 as long as one of these
   *      indexes is missing, e.g. because of duplicates in the data.
   *
   * Every upgrade step is idempotent, and runs under the lock document
   * "SchemaUpgrade" of the "Locks" collection, so that only one
   * Orthanc upgrades a database shared by several of them.
   **/
  class MongoDBSchema : public boost::noncopyable
  {
  public:
    static const unsigned int SCHEMA_VERSION = 6;
    static const unsigned int SCHEMA_REVISION = 2;

    /**
     * Builds the missing indexes. Returns false if a unique index
     * cannot be built because of duplicates, whose name is then added
     * to "missing" (the other indexes are still built).
     **/
    static bool CreateIndexes(std::list<std::string>& missing,
                              DatabaseManager& manager);

    // Returns false, and the names of the missing indexes, if some are missing
    static bool CheckIndexes(std::list<std::string>& missing,
                             DatabaseManager& manager);

    static void Configure(DatabaseManager& manager,
                          const MongoDBParameters& parameters,
                          bool readOnly,
                          uint32_t expectedVersion);
  };
}
