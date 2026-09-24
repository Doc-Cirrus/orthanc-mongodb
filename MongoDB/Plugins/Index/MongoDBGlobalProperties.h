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

#include <string>


namespace OrthancDatabases
{
  /**
   * The "GlobalProperties" collection { property, value } and, for
   * the properties of one server, the "ServerProperties" collection
   * { server, property, value }.
   *
   * A value that does not fit in a BSON document (16MB), such as the
   * serialized jobs of a busy Orthanc, is stored in the GridFS bucket
   * "LargeProperties". Its document then has an empty "value" (which
   * the previous versions of the plugin read as an empty property),
   * and the ID of the GridFS file in the optional field "valueFile".
   **/
  class MongoDBGlobalProperties : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

    mongocxx::gridfs::bucket GetBucket();

    std::string ReadFile(const bsoncxx::types::bson_value::view& file);

    void DeleteFile(const bsoncxx::types::bson_value::view& file);

  public:
    explicit MongoDBGlobalProperties(DatabaseManager& manager);

    bool LookupGlobalProperty(std::string& target /*out*/,
                              const char* serverIdentifier,
                              int32_t property);

    void SetGlobalProperty(const char* serverIdentifier,
                           int32_t property,
                           const char* utf8);

    /**
     * Adds "increment" to an integer property (missing properties are
     * 0), and returns the new value, in one atomic operation. The value
     * stays a string, as for "SetGlobalProperty()".
     **/
    int64_t IncrementGlobalProperty(const char* serverIdentifier,
                                    int32_t property,
                                    int64_t increment);
  };
}
