/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017  (Doc Cirrus GmbH)   Ronald Wertlen, Ihor Mozil
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

#include "MongoDBBackend.h"

#include <orthanc/OrthancCPlugin.h>
#include "Configuration.h"
#include "MongoDBException.h"

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>


namespace OrthancPlugins
{

  static std::string ConvertWildcardToRegex(const std::string& query)
  {
    std::string s = "(?i)^";

    for (size_t i = 0; i < query.size(); i++)
    {
      if (query[i] == '*')
      {
        s += ".*";
      }
      else if (query[i] == '.')
      {
        s += "\\.";
      }
      else if (query[i] == '?')
      {
        s += '.';
      }
      else
      {
        s += query[i];
      }
    }
    s += "$";
    return s;
  }

  MongoDBBackend::MongoDBBackend(OrthancPluginContext* context, MongoDBConnection* connection)
    : context_(context),
    connection_(connection),
    pool_(mongocxx::uri{connection->GetConnectionUri()})
  {
    uint32_t expectedVersion = GlobalProperty_DatabaseSchemaVersion;
    if (context_)
    {
      expectedVersion = OrthancPluginGetExpectedDatabaseVersion(context_);
    }

    /* Check the expected version of the database */
    if (GlobalProperty_DatabaseSchemaVersion != expectedVersion)
    {
      char info[1024];
      sprintf(info, "This database plugin is incompatible with your version of Orthanc "
          "expecting the DB schema version %d, but this plugin is compatible with versions 6",
          expectedVersion);
      OrthancPluginLogError(context_, info);
      throw MongoDBException(info);
    }

    //cache db name
    mongocxx::uri uri{connection->GetConnectionUri()};
    dbname_ = uri.database();

    CheckMonoDBMaster();

    CreateIndices();
  }

  MongoDBBackend::~MongoDBBackend()  {}

  void MongoDBBackend::CheckMonoDBMaster()
  {
    using namespace bsoncxx::builder::stream;
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    auto isMasterDoc = db.run_command(document{} << "isMaster" << 1 << finalize);
    bool isMaster = isMasterDoc.view()["ismaster"].get_bool().value;
    if (!isMaster)
      throw MongoDBException("MongoDB server is not master, could not write.");
  }

  void MongoDBBackend::Open()  {}

  void MongoDBBackend::Close() {}

  void MongoDBBackend::CreateIndices()
  {
    using namespace bsoncxx::builder::stream;
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    db["Resources"].create_index(document{} << "parentId" << 1 << finalize);
    db["Resources"].create_index(document{} << "publicId" << 1 << finalize);
    db["Resources"].create_index(document{} << "resourceType" << 1 << finalize);
    db["Resources"].create_index(document{} << "internalId" << 1 << finalize);
    db["PatientRecyclingOrder"].create_index(document{} << "patientId" << 1 << finalize);
    db["MainDicomTags"].create_index(document{} << "id" << 1 << finalize);
    db["DicomIdentifiers"].create_index(document{} << "id" << 1 << finalize);
    db["DicomIdentifiers"].create_index(document{} << "tagGroup" << 1 << "tagElement" << 1 << finalize);
    db["DicomIdentifiers"].create_index(document{} << "value" << 1 << finalize);
    db["Changes"].create_index(document{} << "internalId" << 1 << finalize);
    db["AttachedFiles"].create_index(document{} << "id" << 1 << finalize);
    db["Metadata"].create_index(document{} << "id" << 1 << finalize);
  }

  void MongoDBBackend::AddAttachment(int64_t id, const OrthancPluginAttachment& attachment)
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    bsoncxx::builder::stream::document document{};

    auto collection = db["AttachedFiles"];
    document << "id" << id
        <<  "fileType" << attachment.contentType
        <<  "uuid" << attachment.uuid
        <<  "compressedSize" << static_cast<int64_t>(attachment.compressedSize)
        <<  "uncompressedSize" << static_cast<int64_t>(attachment.uncompressedSize)
        <<  "compressionType" << attachment.compressionType
        <<  "uncompressedHash" << attachment.uncompressedHash
        <<  "compressedHash" << attachment.compressedHash;

    collection.insert_one(document.view());
  }

  void MongoDBBackend::AttachChild(int64_t parent, int64_t child)
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    auto collection = db["Resources"];

    using namespace bsoncxx::builder::stream;
    collection.update_many(
      document{} << "internalId" << child << finalize,
      document{} << "$set" << open_document <<
            "parentId" << parent << close_document << finalize
    );
  }

  void MongoDBBackend::ClearChanges()
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    auto collection = db["Changes"];
    collection.delete_many(bsoncxx::builder::stream::document{} << bsoncxx::builder::stream::finalize);
  }

  void MongoDBBackend::ClearExportedResources()
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    auto collection = db["ExportedResources"];
    collection.delete_many(bsoncxx::builder::stream::document{} << bsoncxx::builder::stream::finalize);
  }

  int64_t MongoDBBackend::GetNextSequence(mongocxx::database& db, const std::string seqName)
  {
    using namespace bsoncxx::builder::stream;
   
    std::lock_guard<std::mutex> lock(mutex_);

    int64_t num = 1;
    auto collection = db["Sequences"];

    mongocxx::options::find_one_and_update options;
    options.return_document(mongocxx::options::return_document::k_after);
    mongocxx::stdx::optional<bsoncxx::document::value> seqDoc = collection.find_one_and_update(
      document{} << "name" << seqName << finalize,
      document{} << "$inc" << open_document << "i" << int64_t(1) << close_document << finalize,
      options);

    if(seqDoc)
    {
      bsoncxx::document::element element = seqDoc->view()["i"];
      num = element.get_int64().value;
    } else
    {
      collection.insert_one(
        document{} << "name" << seqName << "i" << int64_t(1) << finalize);
    }
    return num;
  }

  int64_t MongoDBBackend::CreateResource(const char* publicId, OrthancPluginResourceType type)
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    bsoncxx::builder::stream::document document{};

    int64_t seq = GetNextSequence(db, "Resources");

    auto collection = db["Resources"];
    document << "internalId" << seq
        <<  "resourceType" << static_cast<int>(type)
        <<  "publicId" << publicId
        <<  "parentId" << bsoncxx::types::b_null();

    collection.insert_one(document.view());
    return seq;
  }

  void MongoDBBackend::DeleteAttachment(int64_t id, int32_t attachment)
  {
   
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    auto collection = db["AttachedFiles"];
    collection.delete_many(
      document{} << "id" << static_cast<int64_t>(id)
          << "fileType" << attachment  << finalize);
  }

  void MongoDBBackend::DeleteMetadata(int64_t id, int32_t metadataType)
  {
   
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    auto collection = db["Metadata"];
    collection.delete_many(
      document{} << "id" << static_cast<int64_t>(id)
          << "type" << metadataType << finalize);
  }

  void MongoDBBackend::DeleteResource(int64_t id)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    /* find all resources to delete */
    std::vector<int64_t> resources_to_delete;
    std::vector<int64_t> parent_resources_vec {id};
    while (!parent_resources_vec.empty())
    {
      int64_t resId = parent_resources_vec.back();
      parent_resources_vec.pop_back();
      resources_to_delete.push_back(resId); // collect the resource id's to delete
      auto resCursor = db["Resources"].find(document{} << "parentId" << resId << finalize);
      for (auto&& d : resCursor)
      {
        int64_t parentId = d["internalId"].get_int64().value;
        parent_resources_vec.push_back(parentId);
      }
    }

    //document with ids to delete
    document inCriteriaArray{};
    auto inCriteriaStream = inCriteriaArray << "$in" << open_array;
    for (auto delId : resources_to_delete)
    {
      inCriteriaStream << delId;
    }
    auto inCriteriaValue = inCriteriaStream << close_array << finalize;
    bsoncxx::document::view_or_value byIdValue = document{} << "id" << inCriteriaValue << finalize;
    bsoncxx::document::view_or_value byInternalIdValue = document{} << "internalId" << inCriteriaValue << finalize;
    bsoncxx::document::view_or_value byPatientIdValue = document{} << "patientId" << inCriteriaValue << finalize;

    // Get the resources to delete for SignalDeletedAttachment
    std::vector<bsoncxx::document::value> deleted_files_vec;
    auto attachedCursor = db["AttachedFiles"].find(byIdValue);
    for (auto&& doc : attachedCursor)
    {
      deleted_files_vec.push_back(bsoncxx::document::value(doc));
    }

    // Get the resources to delete for SignalDeletedResource
    std::vector<bsoncxx::document::value> deleted_resources_vec;
    auto resourcesCursor = db["Resources"].find(document{} << "internalId" << id << finalize);
    for (auto&& doc : resourcesCursor)
    {
      deleted_resources_vec.push_back(bsoncxx::document::value(doc));
    }

    // Delete
    db["Metadata"].delete_many(byIdValue);
    db["AttachedFiles"].delete_many(byIdValue);
    db["Changes"].delete_many(byInternalIdValue);
    db["PatientRecyclingOrder"].delete_many(byPatientIdValue);
    db["MainDicomTags"].delete_many(byIdValue);
    db["DicomIdentifiers"].delete_many(byIdValue);
    db["Resources"].delete_many(byInternalIdValue);

    for (auto&& doc : deleted_files_vec)
    {
      auto v = doc.view();
      GetOutput().SignalDeletedAttachment(v["uuid"].get_utf8().value.to_string().c_str(),
                        v["fileType"].get_int32().value,
                        v["uncompressedSize"].get_int64().value,
                        v["uncompressedHash"].get_utf8().value.to_string().c_str(),
                        v["compressionType"].get_int32().value,
                        v["compressedSize"].get_int64().value,
                        v["compressedHash"].get_utf8().value.to_string().c_str());
    }

    for (auto&& doc : deleted_resources_vec)
    {
      auto v = doc.view();
      GetOutput().SignalDeletedResource(
          v["publicId"].get_utf8().value.to_string().c_str(),
          static_cast<OrthancPluginResourceType>(v["resourceType"].get_int32().value));
    }
  }

  void MongoDBBackend::GetAllInternalIds(std::list<int64_t>& target, OrthancPluginResourceType resourceType)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Resources"].find(
      document{} << "resourceType" << static_cast<int>(resourceType) << finalize);
    for (auto&& doc : cursor)
    {
        target.push_back(doc["internalId"].get_int64().value);
    }
  }

  void MongoDBBackend::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Resources"].find(
      document{} << "resourceType" << static_cast<int>(resourceType) << finalize);
    for (auto&& doc : cursor)
    {
        target.push_back(doc["publicId"].get_utf8().value.to_string());
    }
  }

  void MongoDBBackend::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType,
                     uint64_t since, uint64_t limit)
  {
   
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.limit(limit).skip(since);

    auto cursor = db["Resources"].find(
      document{} << "resourceType" << static_cast<int>(resourceType) << finalize, options);
    for (auto&& doc : cursor)
    {
        target.push_back(doc["publicId"].get_utf8().value.to_string());
    }
  }

  /* Use GetOutput().AnswerChange() */
  void MongoDBBackend::GetChanges(bool& done /*out*/, int64_t since, uint32_t maxResults)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.sort(document{} << "seq" << 1 << finalize).limit(maxResults + 1);

    auto cursor = db["Changes"].find(
      document{} << "id" << open_document << "$gt" << since << close_document << finalize, options);
    uint32_t count = 0;
    done = true;
    for (auto&& doc : cursor)
    {
      if (count == maxResults)
      {
        done = false;
        break;
      }
      GetOutput().AnswerChange(
        doc["id"].get_int64().value,
        doc["changeType"].get_int32().value,
        static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
        GetPublicId(doc["internalId"].get_int64().value),
        doc["date"].get_utf8().value.to_string());
      count++;
    }
  }

  void MongoDBBackend::GetChildrenInternalId(std::list<int64_t>& target /*out*/, int64_t id)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Resources"].find(document{} << "parentId" << id << finalize);
    for (auto&& doc : cursor)
    {
        target.push_back(doc["internalId"].get_int64().value);
    }
  }

  void MongoDBBackend::GetChildrenPublicId(std::list<std::string>& target /*out*/, int64_t id)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Resources"].find(
      document{} << "parentId" << id << finalize);
    for (auto&& doc : cursor)
    {
        target.push_back(doc["publicId"].get_utf8().value.to_string());
    }
  }

  /* Use GetOutput().AnswerExportedResource() */
  void MongoDBBackend::GetExportedResources(bool& done /*out*/, int64_t since, uint32_t maxResults)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.sort(document{} << "id" << 1 << finalize).limit(maxResults + 1);

    auto cursor = db["ExportedResources"].find(
      document{} << "id" << open_document << "$gt" << since << close_document << finalize, options);
    int count = 0;
    done = true;
    for (auto&& doc : cursor)
    {
      if (count == maxResults)
      {
        done = false;
        break;
      }
      GetOutput().AnswerExportedResource(
        doc["id"].get_int64().value,
        static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
        doc["publicId"].get_utf8().value.to_string(),
        doc["remoteModality"].get_utf8().value.to_string(),
        doc["date"].get_utf8().value.to_string(),
        doc["patientId"].get_utf8().value.to_string(),
        doc["studyInstanceUid"].get_utf8().value.to_string(),
        doc["seriesInstanceUid"].get_utf8().value.to_string(),
        doc["sopInstanceUid"].get_utf8().value.to_string());
      count++;
    }
  }

  /* Use GetOutput().AnswerChange() */
  void MongoDBBackend::GetLastChange()
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.sort(document{} << "id" << -1 << finalize).limit(1);

    auto cursor = db["Changes"].find(
      document{} << finalize, options);
    for (auto&& doc : cursor)
    {
      GetOutput().AnswerChange(
        doc["id"].get_int64().value,
        doc["changeType"].get_int32().value,
        static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
        GetPublicId(doc["internalId"].get_int64().value),
        doc["date"].get_utf8().value.to_string());
    }
  }

  /* Use GetOutput().AnswerExportedResource() */
  void MongoDBBackend::GetLastExportedResource()
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.sort(document{} << "id" << -1 << finalize).limit(1);

    auto cursor = db["ExportedResources"].find(document{} << finalize, options);
    for (auto&& doc : cursor)
    {
      GetOutput().AnswerExportedResource(
        doc["id"].get_int64().value,
        static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
        doc["publicId"].get_utf8().value.to_string(),
        doc["remoteModality"].get_utf8().value.to_string(),
        doc["date"].get_utf8().value.to_string(),
        doc["patientId"].get_utf8().value.to_string(),
        doc["studyInstanceUid"].get_utf8().value.to_string(),
        doc["seriesInstanceUid"].get_utf8().value.to_string(),
        doc["sopInstanceUid"].get_utf8().value.to_string());
    }
  }

  /* Use GetOutput().AnswerDicomTag() */
  void MongoDBBackend::GetMainDicomTags(int64_t id)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["MainDicomTags"].find(
      document{} << "id" << id << finalize);
    for (auto&& doc : cursor)
    {
      GetOutput().AnswerDicomTag(static_cast<uint16_t>(doc["tagGroup"].get_int32().value),
                   static_cast<uint16_t>(doc["tagElement"].get_int32().value),
                   doc["value"].get_utf8().value.to_string());
    }
  }

  std::string MongoDBBackend::GetPublicId(int64_t resourceId)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto result = db["Resources"].find_one(document{} << "internalId" << resourceId << finalize);

    if (result)
    {
      return result->view()["publicId"].get_utf8().value.to_string();
    }
    throw MongoDBException("Unknown resource");
  }

  uint64_t MongoDBBackend::GetResourceCount(OrthancPluginResourceType resourceType)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    int64_t count = db["Resources"].count(
      document{} << "resourceType" << static_cast<int>(resourceType) << finalize);
    return count;
  }

  OrthancPluginResourceType MongoDBBackend::GetResourceType(int64_t resourceId)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto result = db["Resources"].find_one(document{} << "internalId" << resourceId << finalize);

    if (result)
    {
      return static_cast<OrthancPluginResourceType>(result->view()["resourceType"].get_int32().value);
    }
    throw MongoDBException("Unknown resource");
  }

  uint64_t MongoDBBackend::GetTotalCompressedSize()
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::pipeline stages;
    bsoncxx::builder::stream::document group_stage;

    group_stage << "_id" << bsoncxx::types::b_null()
          << "totalSize" << open_document << "$sum" << "$compressedSize" << close_document;

    stages.group(group_stage.view());

    auto cursor = db["AttachedFiles"].aggregate(stages);

    for (auto&& doc : cursor)
    {
      return doc["totalSize"].get_int64().value;
    }
   
    return 0;
  }

  uint64_t MongoDBBackend::GetTotalUncompressedSize()
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::pipeline stages;
    bsoncxx::builder::stream::document group_stage;

    group_stage << "_id" << bsoncxx::types::b_null()
          << "totalSize" << open_document << "$sum" << "$uncompressedSize" << close_document;

    stages.group(group_stage.view());

    auto cursor = db["AttachedFiles"].aggregate(stages);

    for (auto&& doc : cursor)
    {
      return doc["totalSize"].get_int64().value;
    }
   
    return 0;  
  }

  bool MongoDBBackend::IsExistingResource(int64_t internalId)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    int64_t count = db["Resources"].count(
      document{} << "internalId" << internalId << finalize);
    return count > 0;
  }

  bool MongoDBBackend::IsProtectedPatient(int64_t internalId)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    int64_t count = db["PatientRecyclingOrder"].count(
      document{} << "patientId" << internalId << finalize);
    return count > 0;
  }

  void MongoDBBackend::ListAvailableMetadata(std::list<int32_t>& target /*out*/, int64_t id)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Metadata"].find(
      document{} << "id" << id << finalize);
    for (auto&& doc : cursor)
    {
        target.push_back(doc["type"].get_int32().value);
    }
  }

  void MongoDBBackend::ListAvailableAttachments(std::list<int32_t>& target /*out*/, int64_t id)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["AttachedFiles"].find(
      document{} << "id" << id << finalize);
    for (auto&& doc : cursor)
    {
        target.push_back(doc["fileType"].get_int32().value);
    }
  }

  void MongoDBBackend::LogChange(const OrthancPluginChange& change)
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    bsoncxx::builder::stream::document document{};

    int64_t seq = GetNextSequence(db, "Changes");

    int64_t id;
    OrthancPluginResourceType type;
    if (!LookupResource(id, type, change.publicId) ||
      type != change.resourceType)
    {
      throw MongoDBException("MongoDBBackend::LogChange - Can not lookup resource.");
    }

    auto collection = db["Changes"];
    document << "id" << seq
        <<  "changeType" << change.changeType
        <<  "internalId" << id
        <<  "resourceType" << change.resourceType
        <<  "date" << change.date;

    collection.insert_one(document.view());
  }

  void MongoDBBackend::LogExportedResource(const OrthancPluginExportedResource& resource)
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    bsoncxx::builder::stream::document document{};

    int64_t seq = GetNextSequence(db, "ExportedResources");

    auto collection = db["ExportedResources"];
    document << "id" << seq
        <<  "resourceType" << resource.resourceType
        <<  "publicId" << resource.publicId
        <<  "remoteModality" << resource.modality
        <<  "patientId" << resource.patientId
        <<  "studyInstanceUid" << resource.studyInstanceUid
        <<  "seriesInstanceUid" << resource.seriesInstanceUid
        <<  "sopInstanceUid" << resource.sopInstanceUid
        <<  "date" << resource.date;

    collection.insert_one(document.view());
  }

  /* Use GetOutput().AnswerAttachment() */
  bool MongoDBBackend::LookupAttachment(int64_t id, int32_t contentType)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto doc = db["AttachedFiles"].find_one(
      document{} << "id" << id << "fileType" << contentType << finalize);
    if(doc)
    {
      bsoncxx::document::view view = doc->view();
      GetOutput().AnswerAttachment(
              view["uuid"].get_utf8().value.to_string(),
              contentType,
              view["uncompressedSize"].get_int64().value,
              view["uncompressedHash"].get_utf8().value.to_string(),
              view["compressionType"].get_int32().value,
              view["compressedSize"].get_int64().value,
              view["compressedHash"].get_utf8().value.to_string());
      return true;
    }
    return false;
  }

  bool MongoDBBackend::LookupGlobalProperty(std::string& target /*out*/, int32_t property)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto doc = db["GlobalProperties"].find_one(
      document{} << "property" << property << finalize);
    if(doc)
    {
        target = doc->view()["value"].get_utf8().value.to_string();
      return true;
    }
    return false;
  }

  void MongoDBBackend::LookupIdentifier(std::list<int64_t>& target /*out*/,
    OrthancPluginResourceType resourceType,
    uint16_t group,
    uint16_t element,
    OrthancPluginIdentifierConstraint constraint,
    const char* value)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    std::list<int64_t> internalIds;
    GetAllInternalIds(internalIds, resourceType);
   
    if (internalIds.size() <= 0)
    {
      //not foundinternalIds
      return;
    }
   
    document in{};
    auto bs = in << "$in" << open_array;
    for (auto rid : internalIds)
    {
      bs << rid;
    }
    auto inValue = bs << close_array << finalize;
    bsoncxx::document::view_or_value criteria;

    switch (constraint)
    {
    case OrthancPluginIdentifierConstraint_Equal:
      criteria = document{} << "id" << inValue
          << "tagGroup" << group
          << "tagElement" << element
          << "value" << value << finalize;
      break;

    case OrthancPluginIdentifierConstraint_SmallerOrEqual:
      criteria = document{} << "id" << inValue
          << "tagGroup" << group
          << "tagElement" << element
          << "value" << open_document << "$lte" << value << close_document << finalize;
      break;

    case OrthancPluginIdentifierConstraint_GreaterOrEqual:
      criteria = document{} << "id" << inValue
          << "tagGroup" << group
          << "tagElement" << element
          << "value" << open_document << "$gte" << value << close_document << finalize;
      break;

    case OrthancPluginIdentifierConstraint_Wildcard:
      criteria = document{} << "id" << inValue
          << "tagGroup" << group
          << "tagElement" << element
          << "value" << open_document << "$regex" << ConvertWildcardToRegex(value) << close_document << finalize;
      break;

    default:
      throw MongoDBException("MongoDBBackend::LookupIdentifier - invalid OrthancPluginIdentifierConstraint");
    }

    auto cursor = db["DicomIdentifiers"].find(criteria.view());
    for (auto&& doc : cursor)
    {
        target.push_back(doc["id"].get_int64().value);
    }

  }

  bool MongoDBBackend::LookupMetadata(std::string& target /*out*/, int64_t id, int32_t metadataType)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto doc = db["Metadata"].find_one(
      document{} << "id" << id << "type" << metadataType << finalize);
    if(doc)
    {
      bsoncxx::document::view view = doc->view();
      target = view["value"].get_utf8().value.to_string();
      return true;
    }
    return false;
  }

  bool MongoDBBackend::LookupParent(int64_t& parentId /*out*/, int64_t resourceId)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    auto doc = db["Resources"].find_one(
      document{} << "internalId" << resourceId << finalize);
    bool res = false;
    if (doc)
    {
      bsoncxx::document::element parent = doc->view()["parentId"];
      if (parent.type() == bsoncxx::type::k_int64)
      {
        parentId = parent.get_int64().value;
        res = true;
      }
    }
    return res;
  }

  bool MongoDBBackend::LookupResource(int64_t& id /*out*/, OrthancPluginResourceType& type /*out*/, const char* publicId)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto doc = db["Resources"].find_one(
      document{} << "publicId" << publicId << finalize);
    if(doc)
    {
      bsoncxx::document::view view = doc->view();
      id = view["internalId"].get_int64().value;
      type = static_cast<OrthancPluginResourceType>(view["resourceType"].get_int32().value);
      return true;
    }
    return false;
  }

  bool MongoDBBackend::SelectPatientToRecycle(int64_t& internalId /*out*/)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto result = db["PatientRecyclingOrder"].find_one(document{} << finalize);

    if (result)
    {
      internalId = result->view()["patientId"].get_int64().value;
      return true;
    }
    return false;
  }

  bool MongoDBBackend::SelectPatientToRecycle(int64_t& internalId /*out*/, int64_t patientIdToAvoid)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto result = db["PatientRecyclingOrder"].find_one(document{} << "patientId" <<
          open_document << "$ne" << patientIdToAvoid << close_document << finalize,
          mongocxx::options::find{}.sort(document{} << "id" << 1 << finalize));

    if (result)
    {
      internalId = result->view()["patientId"].get_int64().value;
      return true;
    }
    return false;
  }

  void MongoDBBackend::SetGlobalProperty(int32_t property, const char* value)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    bsoncxx::builder::stream::document pdocument{};
    pdocument << "property" << property
        <<  "value" << value;

    auto collection = db["GlobalProperties"];
    auto doc = collection.find_one_and_update(
      document{} << "property" << property << finalize,
      pdocument.view()
    );
    if (!doc)
    {
      collection.insert_one(pdocument.view());
    }
  }

  void MongoDBBackend::SetMainDicomTag(int64_t id, uint16_t group, uint16_t element, const char* value)
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    bsoncxx::builder::stream::document document{};

    auto collection = db["MainDicomTags"];
    document << "id" << id
        <<  "tagGroup" << group
        <<  "tagElement" << element
        <<  "value" << value;

    collection.insert_one(document.view());
  }

  void MongoDBBackend::SetIdentifierTag(int64_t id, uint16_t group, uint16_t element, const char* value)
  {
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    bsoncxx::builder::stream::document document{};

    auto collection = db["DicomIdentifiers"];
    document << "id" << id
        <<  "tagGroup" << group
        <<  "tagElement" << element
        <<  "value" << value;

    collection.insert_one(document.view());
  }

  void MongoDBBackend::SetMetadata(int64_t id, int32_t metadataType, const char* value)
  {
    using namespace bsoncxx::builder::stream;
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["Metadata"];

    collection.delete_many(document{} << "id" << id << "type" << metadataType << finalize);
   
    collection.insert_one(
      document{} << "id" << id
        <<  "type" << metadataType
        <<  "value" << value << finalize);
  }

  void MongoDBBackend::SetProtectedPatient(int64_t internalId, bool isProtected)
  {
    using namespace bsoncxx::builder::stream;
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    auto collection = db["PatientRecyclingOrder"];

    if (isProtected)
    {
      if (!IsProtectedPatient(internalId))
      {
        int64_t seq = GetNextSequence(db, "PatientRecyclingOrder");
        collection.insert_one(document{} << "id" << seq
            <<  "patientId" << internalId << finalize);
      }
    }
    else
    {
      collection.delete_many(document{} << "patientId" << internalId << finalize);
    }

  }

  void MongoDBBackend::StartTransaction() {}

  void MongoDBBackend::RollbackTransaction() {}

  void MongoDBBackend::CommitTransaction() {}

  uint32_t MongoDBBackend::GetDatabaseVersion()
  {
    return GlobalProperty_DatabaseSchemaVersion;
  }

  /**
  * Upgrade the database to the specified version of the database
  * schema.  The upgrade script is allowed to make calls to
  * OrthancPluginReconstructMainDicomTags().
  **/
  void MongoDBBackend::UpgradeDatabase(uint32_t  targetVersion, OrthancPluginStorageArea* storageArea)  {}

  void MongoDBBackend::ClearMainDicomTags(int64_t internalId)
  {
    using namespace bsoncxx::builder::stream;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    db["MainDicomTags"].delete_many(document{} << "id" << internalId << finalize);
    db["DicomIdentifiers"].delete_many(document{} << "id" << internalId << finalize);
   
  }

} //namespace OrthancPlugins
