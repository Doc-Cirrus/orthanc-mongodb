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


#include "MongoDBQueues.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"

#include <OrthancException.h>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_array;
  using bsoncxx::builder::basic::make_document;


  static const char* const COLLECTION = "Queues";


  // The values of the queue that are not reserved, or whose reservation has expired
  static bsoncxx::document::value GetAvailableFilter(const std::string& queueId)
  {
    // A missing "reservedUntil" compares below any date
    return make_document(
      kvp("queueId", queueId),
      kvp("$expr", make_document(kvp("$lte", make_array("$reservedUntil", "$$NOW")))));
  }


  static bsoncxx::document::value GetOrder(bool fromFront)
  {
    return make_document(kvp("id", fromFront ? 1 : -1));
  }


  MongoDBQueues::MongoDBQueues(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBQueues::EnqueueValue(const std::string& queueId,
                                   const std::string& value)
  {
    const int64_t id = database_.GetNextSequence(COLLECTION);

    database_.GetCollection(COLLECTION).InsertOne(make_document(
      kvp("queueId", queueId),
      kvp("id", id),
      kvp("value", MongoDBToolbox::ToBinary(value))));
  }


  bool MongoDBQueues::DequeueValue(std::string& value,
                                   const std::string& queueId,
                                   bool fromFront)
  {
    mongocxx::options::find_one_and_delete options;
    options.sort(GetOrder(fromFront));

    std::optional<bsoncxx::document::value> document =
      database_.GetCollection(COLLECTION).FindOneAndDelete(GetAvailableFilter(queueId), options);

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


  uint64_t MongoDBQueues::GetQueueSize(const std::string& queueId)
  {
    return static_cast<uint64_t>(database_.GetCollection(COLLECTION).CountDocuments(make_document(kvp("queueId", queueId))));
  }


  bool MongoDBQueues::ReserveQueueValue(std::string& value,
                                        uint64_t& valueId,
                                        const std::string& queueId,
                                        bool fromFront,
                                        uint32_t reserveTimeout)
  {
    mongocxx::pipeline update;
    update.add_fields(make_document(
      kvp("reservedUntil", make_document(kvp("$add", make_array("$$NOW", static_cast<int64_t>(reserveTimeout) * 1000))))));

    mongocxx::options::find_one_and_update options;
    options.sort(GetOrder(fromFront));

    std::optional<bsoncxx::document::value> document =
      database_.GetCollection(COLLECTION).FindOneAndUpdate(GetAvailableFilter(queueId), update, options);

    if (document)
    {
      value = MongoDBToolbox::GetBinary(document->view(), "value");
      valueId = static_cast<uint64_t>(MongoDBToolbox::GetInteger(document->view(), "id"));
      return true;
    }
    else
    {
      return false;
    }
  }


  void MongoDBQueues::AcknowledgeQueueValue(const std::string& queueId,
                                            uint64_t valueId)
  {
    const int64_t deleted = database_.GetCollection(COLLECTION).DeleteMany(make_document(
      kvp("queueId", queueId),
      kvp("id", static_cast<int64_t>(valueId)),
      kvp("$expr", make_document(kvp("$gt", make_array("$reservedUntil", "$$NOW"))))));

    if (deleted == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource,
                                      "Unable to acknowledge a queue value. Has it expired ?");
    }
  }
}
