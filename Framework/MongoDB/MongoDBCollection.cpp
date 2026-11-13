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


#include "MongoDBCollection.h"

#include "MongoDBDatabase.h"


#define MONGODB_CATCH                           \
  catch (mongocxx::exception& e)                \
  {                                             \
    MongoDBDatabase::ThrowException(e);         \
  }


namespace OrthancDatabases
{
  std::optional<bsoncxx::document::value> MongoDBCollection::FindOne(const bsoncxx::document::view_or_value& filter,
                                                                     const mongocxx::options::find& options)
  {
    try
    {
      if (session_ == NULL)
      {
        return collection_.find_one(filter, options);
      }
      else
      {
        return collection_.find_one(*session_, filter, options);
      }
    }
    MONGODB_CATCH;
  }


  void MongoDBCollection::Find(Documents& target,
                               const bsoncxx::document::view_or_value& filter,
                               const mongocxx::options::find& options)
  {
    target.clear();

    try
    {
      mongocxx::cursor cursor = (session_ == NULL ?
                                 collection_.find(filter, options) :
                                 collection_.find(*session_, filter, options));

      for (auto&& document : cursor)
      {
        target.push_back(bsoncxx::document::value(document));
      }
    }
    MONGODB_CATCH;
  }


  void MongoDBCollection::Aggregate(Documents& target,
                                    const mongocxx::pipeline& pipeline,
                                    const mongocxx::options::aggregate& options)
  {
    target.clear();

    try
    {
      mongocxx::cursor cursor = (session_ == NULL ?
                                 collection_.aggregate(pipeline, options) :
                                 collection_.aggregate(*session_, pipeline, options));

      for (auto&& document : cursor)
      {
        target.push_back(bsoncxx::document::value(document));
      }
    }
    MONGODB_CATCH;
  }


  int64_t MongoDBCollection::CountDocuments(const bsoncxx::document::view_or_value& filter,
                                            const mongocxx::options::count& options)
  {
    try
    {
      if (session_ == NULL)
      {
        return collection_.count_documents(filter, options);
      }
      else
      {
        return collection_.count_documents(*session_, filter, options);
      }
    }
    MONGODB_CATCH;
  }


  bool MongoDBCollection::Exists(const bsoncxx::document::view_or_value& filter)
  {
    mongocxx::options::find options;
    options.projection(bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("_id", 1)));
    return FindOne(filter, options).has_value();
  }


  void MongoDBCollection::InsertOne(const bsoncxx::document::view_or_value& document)
  {
    try
    {
      if (session_ == NULL)
      {
        collection_.insert_one(document);
      }
      else
      {
        collection_.insert_one(*session_, document);
      }
    }
    MONGODB_CATCH;
  }


  void MongoDBCollection::InsertMany(const Documents& documents)
  {
    if (documents.empty())
    {
      return;  // The driver refuses empty insertions
    }

    try
    {
      if (session_ == NULL)
      {
        collection_.insert_many(documents);
      }
      else
      {
        collection_.insert_many(*session_, documents);
      }
    }
    MONGODB_CATCH;
  }


  int64_t MongoDBCollection::UpdateOne(const bsoncxx::document::view_or_value& filter,
                                       const bsoncxx::document::view_or_value& update,
                                       const mongocxx::options::update& options)
  {
    try
    {
      auto result = (session_ == NULL ?
                     collection_.update_one(filter, update, options) :
                     collection_.update_one(*session_, filter, update, options));
      return result ? result->matched_count() : 0;
    }
    MONGODB_CATCH;
  }


  int64_t MongoDBCollection::UpdateMany(const bsoncxx::document::view_or_value& filter,
                                        const bsoncxx::document::view_or_value& update,
                                        const mongocxx::options::update& options)
  {
    try
    {
      auto result = (session_ == NULL ?
                     collection_.update_many(filter, update, options) :
                     collection_.update_many(*session_, filter, update, options));
      return result ? result->matched_count() : 0;
    }
    MONGODB_CATCH;
  }


  std::optional<bsoncxx::document::value> MongoDBCollection::FindOneAndUpdate(
    const bsoncxx::document::view_or_value& filter,
    const bsoncxx::document::view_or_value& update,
    const mongocxx::options::find_one_and_update& options)
  {
    try
    {
      if (session_ == NULL)
      {
        return collection_.find_one_and_update(filter, update, options);
      }
      else
      {
        return collection_.find_one_and_update(*session_, filter, update, options);
      }
    }
    MONGODB_CATCH;
  }


  std::optional<bsoncxx::document::value> MongoDBCollection::FindOneAndUpdate(
    const bsoncxx::document::view_or_value& filter,
    const mongocxx::pipeline& update,
    const mongocxx::options::find_one_and_update& options)
  {
    try
    {
      if (session_ == NULL)
      {
        return collection_.find_one_and_update(filter, update, options);
      }
      else
      {
        return collection_.find_one_and_update(*session_, filter, update, options);
      }
    }
    MONGODB_CATCH;
  }


  std::optional<bsoncxx::document::value> MongoDBCollection::FindOneAndDelete(
    const bsoncxx::document::view_or_value& filter,
    const mongocxx::options::find_one_and_delete& options)
  {
    try
    {
      if (session_ == NULL)
      {
        return collection_.find_one_and_delete(filter, options);
      }
      else
      {
        return collection_.find_one_and_delete(*session_, filter, options);
      }
    }
    MONGODB_CATCH;
  }


  int64_t MongoDBCollection::DeleteMany(const bsoncxx::document::view_or_value& filter)
  {
    try
    {
      auto result = (session_ == NULL ?
                     collection_.delete_many(filter) :
                     collection_.delete_many(*session_, filter));
      return result ? result->deleted_count() : 0;
    }
    MONGODB_CATCH;
  }


  mongocxx::bulk_write MongoDBCollection::CreateBulkWrite(const mongocxx::options::bulk_write& options)
  {
    try
    {
      if (session_ == NULL)
      {
        return collection_.create_bulk_write(options);
      }
      else
      {
        return collection_.create_bulk_write(*session_, options);
      }
    }
    MONGODB_CATCH;
  }


  void MongoDBCollection::Execute(mongocxx::bulk_write& bulk)
  {
    if (bulk.empty())
    {
      return;  // The driver refuses empty bulk writes
    }

    try
    {
      bulk.execute();
    }
    MONGODB_CATCH;
  }
}
