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

#include "MongoDBIncludes.h"

#include <optional>
#include <string>
#include <vector>


namespace OrthancDatabases
{
  /**
   * Thin wrapper around "mongocxx::collection" that:
   *
   * 1. attaches each operation to the session of the active
   *    transaction, if any (otherwise, the operation would run
   *    outside of the transaction, and could even conflict with it);
   *
   * 2. converts the exceptions of the driver into Orthanc errors,
   *    cf. "MongoDBDatabase::ConvertErrorCode()".
   *
   * The cursors are fully read by the methods below, so that no
   * "bsoncxx" view ever outlives the batch it belongs to.
   **/
  class MongoDBCollection
  {
  private:
    mongocxx::collection       collection_;
    mongocxx::client_session*  session_;

  public:
    typedef std::vector<bsoncxx::document::value>  Documents;

    MongoDBCollection(const mongocxx::collection& collection,
                      mongocxx::client_session* session) :
      collection_(collection),
      session_(session)
    {
    }

    const std::string GetName() const
    {
      return std::string(collection_.name());
    }

    std::optional<bsoncxx::document::value> FindOne(const bsoncxx::document::view_or_value& filter,
                                                    const mongocxx::options::find& options = mongocxx::options::find());

    void Find(Documents& target,
              const bsoncxx::document::view_or_value& filter,
              const mongocxx::options::find& options = mongocxx::options::find());

    void Aggregate(Documents& target,
                   const mongocxx::pipeline& pipeline,
                   const mongocxx::options::aggregate& options = mongocxx::options::aggregate());

    int64_t CountDocuments(const bsoncxx::document::view_or_value& filter,
                           const mongocxx::options::count& options = mongocxx::options::count());

    bool Exists(const bsoncxx::document::view_or_value& filter);

    void InsertOne(const bsoncxx::document::view_or_value& document);

    void InsertMany(const Documents& documents);

    // Returns the number of matched documents
    int64_t UpdateOne(const bsoncxx::document::view_or_value& filter,
                      const bsoncxx::document::view_or_value& update,
                      const mongocxx::options::update& options = mongocxx::options::update());

    int64_t UpdateMany(const bsoncxx::document::view_or_value& filter,
                       const bsoncxx::document::view_or_value& update,
                       const mongocxx::options::update& options = mongocxx::options::update());

    std::optional<bsoncxx::document::value> FindOneAndUpdate(
      const bsoncxx::document::view_or_value& filter,
      const bsoncxx::document::view_or_value& update,
      const mongocxx::options::find_one_and_update& options = mongocxx::options::find_one_and_update());

    // Update with an aggregation pipeline, e.g. to compute a field from its current value
    std::optional<bsoncxx::document::value> FindOneAndUpdate(
      const bsoncxx::document::view_or_value& filter,
      const mongocxx::pipeline& update,
      const mongocxx::options::find_one_and_update& options = mongocxx::options::find_one_and_update());

    std::optional<bsoncxx::document::value> FindOneAndDelete(
      const bsoncxx::document::view_or_value& filter,
      const mongocxx::options::find_one_and_delete& options = mongocxx::options::find_one_and_delete());

    // Returns the number of deleted documents
    int64_t DeleteMany(const bsoncxx::document::view_or_value& filter);

    mongocxx::bulk_write CreateBulkWrite(const mongocxx::options::bulk_write& options = mongocxx::options::bulk_write());

    void Execute(mongocxx::bulk_write& bulk);

    // Only for the schema, that never runs inside a transaction
    mongocxx::collection& GetDriverCollection()
    {
      return collection_;
    }
  };
}
