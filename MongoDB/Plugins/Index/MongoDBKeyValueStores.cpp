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


#include "MongoDBKeyValueStores.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  static const char* const COLLECTION = "KeyValueStores";


  MongoDBKeyValueStores::MongoDBKeyValueStores(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBKeyValueStores::StoreKeyValue(const std::string& storeId,
                                            const std::string& key,
                                            const std::string& value)
  {
    // "INSERT ... ON CONFLICT DO UPDATE"
    mongocxx::options::update options;
    options.upsert(true);

    database_.GetCollection(COLLECTION).UpdateOne(
      make_document(kvp("storeId", storeId), kvp("key", key)),
      make_document(kvp("$set", make_document(kvp("value", MongoDBToolbox::ToBinary(value))))),
      options);
  }


  void MongoDBKeyValueStores::DeleteKeyValue(const std::string& storeId,
                                             const std::string& key)
  {
    database_.GetCollection(COLLECTION).DeleteMany(make_document(kvp("storeId", storeId), kvp("key", key)));
  }


  bool MongoDBKeyValueStores::GetKeyValue(std::string& value,
                                          const std::string& storeId,
                                          const std::string& key)
  {
    std::optional<bsoncxx::document::value> document =
      database_.GetCollection(COLLECTION).FindOne(make_document(kvp("storeId", storeId), kvp("key", key)));

    if (document)
    {
      value = MongoDBToolbox::GetBinary(document->view(), "value");
      return true;
    }
    else
    {
      return false;
    }
  }


  void MongoDBKeyValueStores::ListKeysValues(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                             const Orthanc::DatabasePluginMessages::ListKeysValues_Request& request)
  {
    response.mutable_list_keys_values()->Clear();

    // The keys in ascending order, after "from_key" unless "from_first"
    bsoncxx::builder::basic::document filter;
    filter.append(kvp("storeId", request.store_id()));

    if (!request.from_first())
    {
      filter.append(kvp("key", make_document(kvp("$gt", request.from_key()))));
    }

    mongocxx::options::find options;
    options.sort(make_document(kvp("key", 1)));

    if (request.limit() != 0)
    {
      options.limit(static_cast<int64_t>(request.limit()));
    }

    MongoDBCollection::Documents documents;
    database_.GetCollection(COLLECTION).Find(documents, filter.extract(), options);

    for (size_t i = 0; i < documents.size(); i++)
    {
      Orthanc::DatabasePluginMessages::ListKeysValues_Response_KeyValue* item =
        response.mutable_list_keys_values()->add_keys_values();
      item->set_key(MongoDBToolbox::GetString(documents[i].view(), "key"));
      item->set_value(MongoDBToolbox::GetBinary(documents[i].view(), "value"));
    }
  }
}
