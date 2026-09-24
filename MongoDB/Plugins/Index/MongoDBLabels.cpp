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


#include "MongoDBLabels.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  const char* const MongoDBLabels::COLLECTION = "Labels";


  MongoDBLabels::MongoDBLabels(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBLabels::AddLabel(int64_t resource,
                               const std::string& label)
  {
    // "INSERT ... ON CONFLICT DO NOTHING": the upsert is retried by the
    // server if a concurrent call inserts the same label
    mongocxx::options::update options;
    options.upsert(true);

    database_.GetCollection(COLLECTION).UpdateOne(
      make_document(kvp("id", resource), kvp("label", label)),
      make_document(kvp("$setOnInsert", make_document())),
      options);
  }


  void MongoDBLabels::RemoveLabel(int64_t resource,
                                  const std::string& label)
  {
    database_.GetCollection(COLLECTION).DeleteMany(make_document(kvp("id", resource), kvp("label", label)));
  }


  void MongoDBLabels::ListLabels(std::list<std::string>& target,
                                 int64_t resource)
  {
    mongocxx::options::find options;
    options.sort(make_document(kvp("label", 1)));
    options.projection(make_document(kvp("_id", 0), kvp("label", 1)));

    MongoDBCollection::Documents labels;
    database_.GetCollection(COLLECTION).Find(labels, make_document(kvp("id", resource)), options);

    target.clear();
    for (size_t i = 0; i < labels.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetString(labels[i].view(), "label"));
    }
  }


  void MongoDBLabels::ListAllLabels(std::list<std::string>& target)
  {
    // "DISTINCT", on the index { label }
    mongocxx::pipeline pipeline;
    pipeline.group(make_document(kvp("_id", "$label")));
    pipeline.sort(make_document(kvp("_id", 1)));

    MongoDBCollection::Documents labels;
    database_.GetCollection(COLLECTION).Aggregate(labels, pipeline);

    target.clear();
    for (size_t i = 0; i < labels.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetString(labels[i].view(), "_id"));
    }
  }


  void MongoDBLabels::DeleteForResources(const std::list<int64_t>& resources)
  {
    database_.GetCollection(COLLECTION).DeleteMany(
      make_document(kvp("id", make_document(kvp("$in", MongoDBToolbox::ToArray(resources))))));
  }
}
