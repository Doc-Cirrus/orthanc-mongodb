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
#include "../../../Framework/Plugins/MessagesToolbox.h"

#include <string>


namespace OrthancDatabases
{
  /**
   * The "KeyValueStores" collection { storeId, key, value }, with a
   * unique index on (storeId, key). The value is binary, and must fit
   * in a BSON document (16MB).
   **/
  class MongoDBKeyValueStores : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

  public:
    explicit MongoDBKeyValueStores(DatabaseManager& manager);

    void StoreKeyValue(const std::string& storeId,
                       const std::string& key,
                       const std::string& value);

    void DeleteKeyValue(const std::string& storeId,
                        const std::string& key);

    bool GetKeyValue(std::string& value,
                     const std::string& storeId,
                     const std::string& key);

    void ListKeysValues(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                        const Orthanc::DatabasePluginMessages::ListKeysValues_Request& request);
  };
}
