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


#include "MongoDBExportedResources.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  static const char* const COLLECTION = "ExportedResources";


  MongoDBExportedResources::MongoDBExportedResources(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBExportedResources::Answer(IDatabaseBackendOutput& output,
                                        bool& done,
                                        const bsoncxx::document::view_or_value& filter,
                                        int sort,
                                        uint32_t maxResults)
  {
    mongocxx::options::find options;
    options.sort(make_document(kvp("id", sort)));
    options.limit(static_cast<int64_t>(maxResults) + 1);

    MongoDBCollection::Documents exported;
    database_.GetCollection(COLLECTION).Find(exported, filter, options);

    done = (exported.size() <= maxResults);

    for (size_t i = 0; i < exported.size() && i < maxResults; i++)
    {
      const bsoncxx::document::view item = exported[i].view();

      output.AnswerExportedResource(MongoDBToolbox::GetInteger(item, "id"),
                                    static_cast<OrthancPluginResourceType>(MongoDBToolbox::GetInt32(item, "resourceType")),
                                    MongoDBToolbox::GetString(item, "publicId"),
                                    MongoDBToolbox::GetString(item, "remoteModality"),
                                    MongoDBToolbox::GetString(item, "date"),
                                    MongoDBToolbox::GetString(item, "patientId"),
                                    MongoDBToolbox::GetString(item, "studyInstanceUid"),
                                    MongoDBToolbox::GetString(item, "seriesInstanceUid"),
                                    MongoDBToolbox::GetString(item, "sopInstanceUid"));
    }
  }


  void MongoDBExportedResources::LogExportedResource(OrthancPluginResourceType resourceType,
                                                     const char* publicId,
                                                     const char* modality,
                                                     const char* date,
                                                     const char* patientId,
                                                     const char* studyInstanceUid,
                                                     const char* seriesInstanceUid,
                                                     const char* sopInstanceUid)
  {
    const int64_t seq = database_.GetNextSequence(COLLECTION);

    database_.GetCollection(COLLECTION).InsertOne(make_document(
      kvp("id", seq),
      kvp("resourceType", resourceType),
      kvp("publicId", publicId),
      kvp("remoteModality", modality),
      kvp("patientId", patientId),
      kvp("studyInstanceUid", studyInstanceUid),
      kvp("seriesInstanceUid", seriesInstanceUid),
      kvp("sopInstanceUid", sopInstanceUid),
      kvp("date", date)));
  }


  void MongoDBExportedResources::GetExportedResources(IDatabaseBackendOutput& output,
                                                      bool& done /*out*/,
                                                      int64_t since,
                                                      uint32_t maxResults)
  {
    Answer(output, done, make_document(kvp("id", make_document(kvp("$gt", since)))), 1, maxResults);
  }


  void MongoDBExportedResources::GetLastExportedResource(IDatabaseBackendOutput& output)
  {
    bool done;
    Answer(output, done, make_document(), -1, 1);
  }


  void MongoDBExportedResources::ClearExportedResources()
  {
    database_.GetCollection(COLLECTION).DeleteMany(make_document());
  }
}
