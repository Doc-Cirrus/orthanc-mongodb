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

#include <list>
#include <string>


namespace OrthancDatabases
{
  /**
   * Accessors to the fields of the documents stored by the plugin.
   * They throw "ErrorCode_Database" if a mandatory field is missing
   * or has an unexpected type, instead of the "bsoncxx" exceptions.
   **/
  namespace MongoDBToolbox
  {
    // Accepts both 32-bit and 64-bit integers
    int64_t GetInteger(const bsoncxx::document::view& document,
                       const std::string& key);

    // Returns "false" if the field is missing or null
    bool LookupInteger(int64_t& target,
                       const bsoncxx::document::view& document,
                       const std::string& key);

    int32_t GetInt32(const bsoncxx::document::view& document,
                     const std::string& key);

    std::string GetString(const bsoncxx::document::view& document,
                          const std::string& key);

    // Returns "false" if the field is missing or is not a string
    bool LookupString(std::string& target,
                      const bsoncxx::document::view& document,
                      const std::string& key);

    // The "revision" field is missing in the documents written by old releases
    int64_t GetRevision(const bsoncxx::document::view& document);

    bsoncxx::array::value ToArray(const std::list<int64_t>& values);

    // A byte string, which must outlive the result (the values of the key-value stores and queues)
    bsoncxx::types::b_binary ToBinary(const std::string& value);

    // Accepts a binary field and, for documents written by hand, a string
    std::string GetBinary(const bsoncxx::document::view& document,
                          const std::string& key);
  }
}
