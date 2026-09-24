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


#include "MongoDBChanges.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  static const char* const COLLECTION = "Changes";


  MongoDBChanges::MongoDBChanges(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBChanges::LogChange(int32_t changeType,
                                 int64_t resourceId,
                                 OrthancPluginResourceType resourceType,
                                 const char* date)
  {
    const int64_t seq = database_.GetNextSequence(COLLECTION);

    database_.GetCollection(COLLECTION).InsertOne(make_document(
      kvp("id", seq),
      kvp("changeType", changeType),
      kvp("internalId", resourceId),
      kvp("resourceType", resourceType),
      kvp("date", date)));
  }


  /**
   * Reads the changes of "pipeline", together with the public ID of
   * their resource (one query instead of one per change). If
   * "descending", the pipeline gives the most recent changes first,
   * and they are answered in ascending order.
   **/
  static void AnswerChanges(IDatabaseBackendOutput& output,
                            MongoDBDatabase& database,
                            mongocxx::pipeline& pipeline,
                            uint32_t maxResults,
                            bool& done,
                            bool descending = false)
  {
    pipeline.lookup(make_document(
      kvp("from", "Resources"),
      kvp("localField", "internalId"),
      kvp("foreignField", "internalId"),
      kvp("as", "resource")));

    MongoDBCollection::Documents changes;
    database.GetCollection(COLLECTION).Aggregate(changes, pipeline);

    done = (changes.size() <= maxResults);

    if (changes.size() > maxResults)
    {
      changes.erase(changes.begin() + maxResults, changes.end());  // The extra change only tells whether there are more
    }

    for (size_t k = 0; k < changes.size(); k++)
    {
      const bsoncxx::document::view change = changes[descending ? changes.size() - 1 - k : k].view();

      std::string publicId;
      bsoncxx::document::element resource = change["resource"];
      if (resource.type() == bsoncxx::type::k_array &&
          !resource.get_array().value.empty())
      {
        publicId = MongoDBToolbox::GetString(resource.get_array().value[0].get_document().value, "publicId");
      }

      output.AnswerChange(MongoDBToolbox::GetInteger(change, "id"),
                          MongoDBToolbox::GetInt32(change, "changeType"),
                          static_cast<OrthancPluginResourceType>(MongoDBToolbox::GetInt32(change, "resourceType")),
                          publicId,
                          MongoDBToolbox::GetString(change, "date"));
    }
  }


  void MongoDBChanges::GetChanges(IDatabaseBackendOutput& output,
                                  bool& done /*out*/,
                                  int64_t since,
                                  uint32_t maxResults)
  {
    mongocxx::pipeline pipeline;
    pipeline.match(make_document(kvp("id", make_document(kvp("$gt", since)))));
    pipeline.sort(make_document(kvp("id", 1)));
    pipeline.limit(static_cast<int64_t>(maxResults) + 1);

    AnswerChanges(output, database_, pipeline, maxResults, done);
  }


  void MongoDBChanges::GetChangesExtended(IDatabaseBackendOutput& output,
                                          bool& done /*out*/,
                                          int64_t since,
                                          int64_t to,
                                          const std::set<uint32_t>& changeTypes,
                                          uint32_t maxResults)
  {
    // Same filters as "IndexBackend::GetChangesExtended()" of the SQL plugins
    bsoncxx::builder::basic::document range;
    const bool hasSince = (since > 0);
    const bool hasTo = (to != -1);

    if (hasSince)
    {
      range.append(kvp("$gt", since));
    }

    if (hasTo)
    {
      range.append(kvp("$lte", to));
    }

    bsoncxx::builder::basic::document filter;

    if (hasSince || hasTo)
    {
      filter.append(kvp("id", range.extract()));
    }

    if (!changeTypes.empty())
    {
      bsoncxx::builder::basic::array types;
      for (std::set<uint32_t>::const_iterator it = changeTypes.begin(); it != changeTypes.end(); ++it)
      {
        types.append(static_cast<int32_t>(*it));
      }

      filter.append(kvp("changeType", make_document(kvp("$in", types.extract()))));
    }

    // Without "since", a "to" asks for the most recent changes before it
    const bool descending = (hasTo && !hasSince);

    mongocxx::pipeline pipeline;
    pipeline.match(filter.extract());
    pipeline.sort(make_document(kvp("id", descending ? -1 : 1)));
    pipeline.limit(static_cast<int64_t>(maxResults) + 1);

    AnswerChanges(output, database_, pipeline, maxResults, done, descending);
  }


  void MongoDBChanges::GetLastChange(IDatabaseBackendOutput& output)
  {
    mongocxx::pipeline pipeline;
    pipeline.sort(make_document(kvp("id", -1)));
    pipeline.limit(1);

    bool done;
    AnswerChanges(output, database_, pipeline, 1, done);
  }


  void MongoDBChanges::ClearChanges()
  {
    database_.GetCollection(COLLECTION).DeleteMany(make_document());
  }


  int64_t MongoDBChanges::GetLastChangeIndex()
  {
    // The changes of deleted resources are removed, so the last index
    // comes from the sequence, as "sqlite_sequence" does in SQLite
    std::optional<bsoncxx::document::value> sequence =
      database_.GetCollection("Sequences").FindOne(make_document(kvp("name", COLLECTION)));

    if (sequence)
    {
      return MongoDBToolbox::GetInteger(sequence->view(), "i");
    }
    else
    {
      return 0;
    }
  }


  void MongoDBChanges::DeleteForResources(const std::list<int64_t>& resources)
  {
    database_.GetCollection(COLLECTION).DeleteMany(
      make_document(kvp("internalId", make_document(kvp("$in", MongoDBToolbox::ToArray(resources))))));
  }
}
