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


#include "MongoDBMetadata.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  static const char* const COLLECTION = "Metadata";


  MongoDBMetadata::MongoDBMetadata(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBMetadata::SetMetadata(int64_t id,
                                    int32_t metadataType,
                                    const char* value,
                                    int64_t revision)
  {
    // Replaces the existing value, if any, in a single operation.
    // "UpdateMany()" also fixes the duplicates that older releases
    // could create under concurrency.
    mongocxx::options::update options;
    options.upsert(true);

    database_.GetCollection(COLLECTION).UpdateMany(
      make_document(kvp("id", id), kvp("type", metadataType)),
      make_document(kvp("$set", make_document(kvp("value", value), kvp("revision", revision)))),
      options);
  }


  void MongoDBMetadata::SetMetadata(uint32_t count,
                                    const OrthancPluginResourcesContentMetadata* metadata)
  {
    MongoDBCollection collection = database_.GetCollection(COLLECTION);

    mongocxx::options::bulk_write options;
    options.ordered(true);
    mongocxx::bulk_write bulk = collection.CreateBulkWrite(options);

    for (uint32_t i = 0; i < count; i++)
    {
      mongocxx::model::update_many upsert(
        make_document(kvp("id", metadata[i].resource), kvp("type", metadata[i].metadata)),
        make_document(kvp("$set", make_document(kvp("value", metadata[i].value), kvp("revision", int64_t(0))))));
      upsert.upsert(true);
      bulk.append(upsert);
    }

    collection.Execute(bulk);
  }


  void MongoDBMetadata::DeleteMetadata(int64_t id,
                                       int32_t metadataType)
  {
    database_.GetCollection(COLLECTION).DeleteMany(make_document(kvp("id", id), kvp("type", metadataType)));
  }


  bool MongoDBMetadata::LookupMetadata(std::string& target /*out*/,
                                       int64_t& revision /*out*/,
                                       int64_t id,
                                       int32_t metadataType)
  {
    std::optional<bsoncxx::document::value> document =
      database_.GetCollection(COLLECTION).FindOne(make_document(kvp("id", id), kvp("type", metadataType)));

    if (document)
    {
      target = MongoDBToolbox::GetString(document->view(), "value");
      revision = MongoDBToolbox::GetRevision(document->view());
      return true;
    }
    else
    {
      return false;
    }
  }


  void MongoDBMetadata::ListAvailableMetadata(std::list<int32_t>& target /*out*/,
                                              int64_t id)
  {
    target.clear();

    MongoDBCollection::Documents metadata;
    database_.GetCollection(COLLECTION).Find(metadata, make_document(kvp("id", id)));

    for (size_t i = 0; i < metadata.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetInt32(metadata[i].view(), "type"));
    }
  }


  void MongoDBMetadata::GetAllMetadata(std::map<int32_t, std::string>& result /*out*/,
                                       int64_t id)
  {
    result.clear();

    MongoDBCollection::Documents metadata;
    database_.GetCollection(COLLECTION).Find(metadata, make_document(kvp("id", id)));

    for (size_t i = 0; i < metadata.size(); i++)
    {
      result[MongoDBToolbox::GetInt32(metadata[i].view(), "type")] =
        MongoDBToolbox::GetString(metadata[i].view(), "value");
    }
  }


  void MongoDBMetadata::GetMetadataOfResources(std::list<std::string>& target /*out*/,
                                               const std::list<int64_t>& resources,
                                               int32_t metadataType)
  {
    target.clear();

    MongoDBCollection::Documents metadata;
    database_.GetCollection(COLLECTION).Find(metadata, make_document(
      kvp("type", metadataType),
      kvp("id", make_document(kvp("$in", MongoDBToolbox::ToArray(resources))))));

    for (size_t i = 0; i < metadata.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetString(metadata[i].view(), "value"));
    }
  }


  void MongoDBMetadata::DeleteForResources(const std::list<int64_t>& resources)
  {
    database_.GetCollection(COLLECTION).DeleteMany(
      make_document(kvp("id", make_document(kvp("$in", MongoDBToolbox::ToArray(resources))))));
  }
}
