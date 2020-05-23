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
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto isMasterDoc = db.run_command(make_document(kvp("isMaster", 1)));

    bool isMaster = isMasterDoc.view()["ismaster"].get_bool().value;

    if (!isMaster)
      throw MongoDBException("MongoDB server is not master, could not write.");
  }

  void MongoDBBackend::Open()  {}

  void MongoDBBackend::Close() {}

  void MongoDBBackend::CreateIndices()
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    
    db["Resources"].create_index(make_document(kvp("parentId", 1)));
    db["Resources"].create_index(make_document(kvp("publicId", 1)));
    db["Resources"].create_index(make_document(kvp("resourceType", 1)));
    db["Resources"].create_index(make_document(kvp("internalId", 1)));
    db["PatientRecyclingOrder"].create_index(make_document(kvp("patientId", 1)));
    db["MainDicomTags"].create_index(make_document(kvp("id", 1)));
    db["DicomIdentifiers"].create_index(make_document(kvp("id", 1)));
    db["DicomIdentifiers"].create_index(make_document(kvp("tagGroup", 1), kvp("tagElement", 1)));
    db["DicomIdentifiers"].create_index(make_document(kvp("value", 1)));
    db["Changes"].create_index(make_document(kvp("internalId", 1)));
    db["AttachedFiles"].create_index(make_document(kvp("id", 1)));
    db["Metadata"].create_index(make_document(kvp("id", 1)));
  }

  void MongoDBBackend::AddAttachment(int64_t id, const OrthancPluginAttachment& attachment)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["AttachedFiles"];
    auto attachment_document = make_document(
      kvp("id", id),
      kvp("fileType", attachment.contentType),
      kvp("uuid", attachment.uuid),
      kvp("compressedSize", static_cast<int64_t>(attachment.compressedSize)),
      kvp("uncompressedSize", static_cast<int64_t>(attachment.uncompressedSize)),
      kvp("compressionType", attachment.compressionType),
      kvp("uncompressedHash", attachment.uncompressedHash),
      kvp("compressedHash", attachment.compressedHash)
    );

    collection.insert_one(attachment_document.view());
  }

  void MongoDBBackend::AttachChild(int64_t parent, int64_t child)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["Resources"];

    collection.update_many(
      make_document(kvp("internalId", child)),
      make_document(kvp("$set", make_document(kvp("parentId", parent))))
    );
  }

  void MongoDBBackend::ClearChanges()
  {
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["Changes"];
    collection.delete_many({});
  }

  void MongoDBBackend::ClearExportedResources()
  {
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["ExportedResources"];
    collection.delete_many({});
  }

  int64_t MongoDBBackend::GetNextSequence(mongocxx::database& db, const std::string seqName)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;
   
    std::lock_guard<std::mutex> lock(mutex_);

    int64_t num = 1;
    auto collection = db["Sequences"];

    mongocxx::options::find_one_and_update options;
    options.return_document(mongocxx::options::return_document::k_after);
    mongocxx::stdx::optional<bsoncxx::document::value> seqDoc = collection.find_one_and_update(
      make_document(kvp("name", seqName)),
      make_document(kvp("$inc", make_document(kvp("i", int64_t(1))))),
      options
    );

    if(seqDoc) {
      bsoncxx::document::element element = seqDoc->view()["i"];
      num = element.get_int64().value;
    } 

    else {
      collection.insert_one(
        make_document(
          kvp("name", seqName), 
          kvp("i", int64_t(1))
      ));
    }
    return num;
  }

  int64_t MongoDBBackend::CreateResource(const char* publicId, OrthancPluginResourceType type)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    int64_t seq = GetNextSequence(db, "Resources");

    auto collection = db["Resources"];
    auto resource_document = make_document(
      kvp("internalId", seq),
      kvp("resourceType", static_cast<int>(type)),
      kvp("publicId", publicId),
      kvp("parentId", bsoncxx::types::b_null())
    );

    collection.insert_one(resource_document.view());

    if (type == OrthancPluginResourceType_Patient) {
      /**
       * Patient must be added to PatientRecyclingOrder when it is created.
       * Also if patient is created via API function - CreateInstance,
       * all required logic for PatientRecyclingOrder is handled inside CreateInstance function.
       **/
      int64_t id = GetNextSequence(db, "PatientRecyclingOrder");
      db["PatientRecyclingOrder"].insert_one(make_document(
        kvp("id", id),
        kvp("patientId", seq)
      ));
    }

    return seq;
  }

  void MongoDBBackend::DeleteAttachment(int64_t id, int32_t attachment)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["AttachedFiles"];

    collection.delete_many(make_document(
      kvp("id", static_cast<int64_t>(id)),
      kvp("fileType", attachment)
    ));
  }

  void MongoDBBackend::DeleteMetadata(int64_t id, int32_t metadataType)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["Metadata"];

    collection.delete_many(make_document(
      kvp("id", static_cast<int64_t>(id)),
      kvp("type", metadataType)
    ));
  }

  // TODO BETTER Refactor
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
      GetOutput().SignalDeletedAttachment(std::string(v["uuid"].get_utf8().value).c_str(),
                        v["fileType"].get_int32().value,
                        v["uncompressedSize"].get_int64().value,
						std::string(v["uncompressedHash"].get_utf8().value).c_str(),
                        v["compressionType"].get_int32().value,
                        v["compressedSize"].get_int64().value,
						std::string(v["compressedHash"].get_utf8().value).c_str());
    }

    for (auto&& doc : deleted_resources_vec)
    {
      auto v = doc.view();
      GetOutput().SignalDeletedResource(
		  std::string(v["publicId"].get_utf8().value).c_str(),
          static_cast<OrthancPluginResourceType>(v["resourceType"].get_int32().value));
    }
  }

  void MongoDBBackend::GetAllInternalIds(std::list<int64_t>& target, OrthancPluginResourceType resourceType)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Resources"].find(
      make_document(kvp("resourceType", static_cast<int>(resourceType)))
    );

    for (auto&& doc : cursor)
    {
      target.push_back(doc["internalId"].get_int64().value);
    }
  }

  void MongoDBBackend::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Resources"].find(
      make_document(kvp("resourceType", static_cast<int>(resourceType)))
    );

    for (auto&& doc : cursor)
    {
      target.push_back(std::string(doc["publicId"].get_utf8().value));
    }
  }

  void MongoDBBackend::GetAllPublicIds(
    std::list<std::string>& target, OrthancPluginResourceType resourceType,
    uint64_t since, uint64_t limit)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.limit(limit).skip(since);

    auto cursor = db["Resources"].find(
      make_document(kvp("resourceType", static_cast<int>(resourceType))), options
    );

    for (auto&& doc : cursor)
    {
      target.push_back(std::string(doc["publicId"].get_utf8().value));
    }
  }

  /* Use GetOutput().AnswerChange() */
  void MongoDBBackend::GetChanges(bool& done /*out*/, int64_t since, uint32_t maxResults)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.sort(make_document(kvp("seq", 1))).limit(maxResults + 1);

    done = true;
    uint32_t count = 0;

    auto cursor = db["Changes"].find(
      make_document(kvp("id", make_document(kvp("$gt", since)))), options
    );

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
		    std::string(doc["date"].get_utf8().value)
      );

      count++;
    }
  }

  void MongoDBBackend::GetChildrenInternalId(std::list<int64_t>& target /*out*/, int64_t id)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Resources"].find(make_document(kvp("parentId", id)));

    for (auto&& doc : cursor)
    {
      target.push_back(doc["internalId"].get_int64().value);
    }
  }

  void MongoDBBackend::GetChildrenPublicId(std::list<std::string>& target /*out*/, int64_t id)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Resources"].find(make_document(kvp("parentId", id)));

    for (auto&& doc : cursor)
    {
      target.push_back(std::string(doc["publicId"].get_utf8().value));
    }
  }

  /* Use GetOutput().AnswerExportedResource() */
  void MongoDBBackend::GetExportedResources(bool& done /*out*/, int64_t since, uint32_t maxResults)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.sort(make_document(kvp("id",  1))).limit(maxResults + 1);

    done = true;
    int count = 0;
    auto cursor = db["ExportedResources"].find(make_document(kvp("id", make_document(kvp("$gt", since)))), options);

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
        std::string(doc["publicId"].get_utf8().value),
        std::string(doc["remoteModality"].get_utf8().value),
        std::string(doc["date"].get_utf8().value),
        std::string(doc["patientId"].get_utf8().value),
        std::string(doc["studyInstanceUid"].get_utf8().value),
        std::string(doc["seriesInstanceUid"].get_utf8().value),
        std::string(doc["sopInstanceUid"].get_utf8().value)
      );

      count++;
    }
  }

  /* Use GetOutput().AnswerChange() */
  void MongoDBBackend::GetLastChange()
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.sort(make_document(kvp("id",  -1))).limit(1);

    auto cursor = db["Changes"].find({}, options);

    for (auto&& doc : cursor)
    {
      GetOutput().AnswerChange(
        doc["id"].get_int64().value,
        doc["changeType"].get_int32().value,
        static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
        GetPublicId(doc["internalId"].get_int64().value),
		    std::string(doc["date"].get_utf8().value)
      );
    }
  }

  /* Use GetOutput().AnswerExportedResource() */
  void MongoDBBackend::GetLastExportedResource()
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::options::find options{};
    options.sort(make_document(kvp("id",  -1))).limit(1);

    auto cursor = db["ExportedResources"].find({}, options);
    for (auto&& doc : cursor)
    {
      GetOutput().AnswerExportedResource(
        doc["id"].get_int64().value,
        static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
        std::string(doc["publicId"].get_utf8().value),
        std::string(doc["remoteModality"].get_utf8().value),
        std::string(doc["date"].get_utf8().value),
        std::string(doc["patientId"].get_utf8().value),
        std::string(doc["studyInstanceUid"].get_utf8().value),
        std::string(doc["seriesInstanceUid"].get_utf8().value),
        std::string(doc["sopInstanceUid"].get_utf8().value)
      );
    }
  }

  /* Use GetOutput().AnswerDicomTag() */
  void MongoDBBackend::GetMainDicomTags(int64_t id)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["MainDicomTags"].find(make_document(kvp("id",  id)));

    for (auto&& doc : cursor)
    {
      GetOutput().AnswerDicomTag(
        static_cast<uint16_t>(doc["tagGroup"].get_int32().value),
        static_cast<uint16_t>(doc["tagElement"].get_int32().value),
				std::string(doc["value"].get_utf8().value)
      );
    }
  }

  std::string MongoDBBackend::GetPublicId(int64_t resourceId)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto result = db["Resources"].find_one(make_document(kvp("internalId", resourceId)));

    if (result)
    {
      return std::string(result->view()["publicId"].get_utf8().value);
    }
    throw MongoDBException("Unknown resource");
  }

  uint64_t MongoDBBackend::GetResourceCount(OrthancPluginResourceType resourceType)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    int64_t count = db["Resources"].count_documents(
      make_document(kvp("resourceType", static_cast<int>(resourceType)))
    );

    return count;
  }

  OrthancPluginResourceType MongoDBBackend::GetResourceType(int64_t resourceId)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto result = db["Resources"].find_one(make_document(kvp("internalId", resourceId)));

    if (result)
    {
      return static_cast<OrthancPluginResourceType>(result->view()["resourceType"].get_int32().value);
    }
    throw MongoDBException("Unknown resource");
  }

  uint64_t MongoDBBackend::GetTotalCompressedSize()
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::pipeline stages;
    auto group_stage = make_document(
      kvp("_id", bsoncxx::types::b_null()),
      kvp("totalSize", make_document(
        kvp("$sum", "$compressedSize" )
      ))
    );

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
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::pipeline stages;
    auto group_stage = make_document(
      kvp("_id", bsoncxx::types::b_null()),
      kvp("totalSize", make_document(
        kvp("$sum", "$uncompressedSize" )
      ))
    );

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
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    int64_t count = db["Resources"].count_documents(
      make_document(kvp("internalId", internalId))
    );

    return count > 0;
  }

  bool MongoDBBackend::IsProtectedPatient(int64_t internalId)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    int64_t count = db["PatientRecyclingOrder"].count_documents(
      make_document(kvp("patientId", internalId))
    );

    return !count;
  }

  void MongoDBBackend::ListAvailableMetadata(std::list<int32_t>& target /*out*/, int64_t id)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["Metadata"].find(make_document(kvp("id", id)));
    
    for (auto&& doc : cursor)
    {
        target.push_back(doc["type"].get_int32().value);
    }
  }

  void MongoDBBackend::ListAvailableAttachments(std::list<int32_t>& target /*out*/, int64_t id)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto cursor = db["AttachedFiles"].find(make_document(kvp("id", id)));

    for (auto&& doc : cursor)
    {
        target.push_back(doc["fileType"].get_int32().value);
    }
  }

  void MongoDBBackend::LogChange(const OrthancPluginChange& change)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    int64_t id;
    OrthancPluginResourceType type;

    if (!LookupResource(id, type, change.publicId) || type != change.resourceType)
    {
      throw MongoDBException("MongoDBBackend::LogChange - Can not lookup resource.");
    }

    auto collection = db["Changes"];
    int64_t seq = GetNextSequence(db, "Changes");

    auto change_document = make_document(
      kvp("id", seq),
      kvp("changeType", change.changeType),
      kvp("internalId", id),
      kvp("resourceType", change.resourceType),
      kvp("date", change.date)
    );

    collection.insert_one(change_document.view());
  }

  void MongoDBBackend::LogExportedResource(const OrthancPluginExportedResource& resource)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    int64_t seq = GetNextSequence(db, "ExportedResources");

    auto collection = db["ExportedResources"];
    auto exported_document = make_document(
      kvp("id", seq),
      kvp("resourceType", resource.resourceType),
      kvp("publicId", resource.publicId),
      kvp("remoteModality", resource.modality),
      kvp("patientId", resource.patientId),
      kvp("studyInstanceUid", resource.studyInstanceUid),
      kvp("seriesInstanceUid", resource.seriesInstanceUid),
      kvp("sopInstanceUid", resource.sopInstanceUid),
      kvp("date", resource.date)
    );

    collection.insert_one(exported_document.view());
  }

  /* Use GetOutput().AnswerAttachment() */
  bool MongoDBBackend::LookupAttachment(int64_t id, int32_t contentType)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto doc = db["AttachedFiles"].find_one(make_document(
      kvp("id", id), kvp("fileType", contentType)
    ));
    
    if(doc)
    {
      bsoncxx::document::view view = doc->view();

      GetOutput().AnswerAttachment(
        std::string(view["uuid"].get_utf8().value),
        contentType,
        view["uncompressedSize"].get_int64().value,
        std::string(view["uncompressedHash"].get_utf8().value),
        view["compressionType"].get_int32().value,
        view["compressedSize"].get_int64().value,
        std::string(view["compressedHash"].get_utf8().value)
      );

      return true;
    }
    return false;
  }

  bool MongoDBBackend::LookupGlobalProperty(std::string& target /*out*/, int32_t property)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto doc = db["GlobalProperties"].find_one(
      make_document(kvp("property", property))
    );

    if(doc)
    {
      target = std::string(doc->view()["value"].get_utf8().value);
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
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    bsoncxx::document::view_or_value criteria;

    switch (constraint)
    {
      case OrthancPluginIdentifierConstraint_Equal:
        criteria = make_document(
          kvp("tagGroup", group),
          kvp("tagElement", element),
          kvp("value", value)
        );
        break;

      case OrthancPluginIdentifierConstraint_SmallerOrEqual:
        criteria = make_document(
          kvp("tagGroup", group),
          kvp("tagElement", element),
          kvp("value", make_document(kvp("$lte", value)))
        );
        break;

      case OrthancPluginIdentifierConstraint_GreaterOrEqual:
        criteria = make_document(
          kvp("tagGroup", group),
          kvp("tagElement", element),
          kvp("value", make_document(kvp("$gte", value)))
        );
        break;

      case OrthancPluginIdentifierConstraint_Wildcard:
        criteria = make_document(
          kvp("tagGroup", group),
          kvp("tagElement", element),
          kvp("value", make_document(kvp("$regex", ConvertWildcardToRegex(value))))
        );
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

 void MongoDBBackend::LookupIdentifierRange(std::list<int64_t>& target,
      OrthancPluginResourceType resourceType,
      uint16_t group,
      uint16_t element,
      const char* start,
      const char* end)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto criteria = make_document(
      kvp("tagGroup", group),
      kvp("tagElement", element),
      kvp("value", make_document(
        kvp("$gte", start), kvp("$lte", end)
      ))
    );

    auto cursor = db["DicomIdentifiers"].find(criteria.view());
    for (auto&& doc : cursor)
    {
        target.push_back(doc["id"].get_int64().value);
    }
  }

  bool MongoDBBackend::LookupMetadata(std::string& target /*out*/, int64_t id, int32_t metadataType)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto doc = db["Metadata"].find_one(make_document(kvp("id", id), kvp("type", metadataType)));

    if(doc)
    {
      bsoncxx::document::view view = doc->view();
      target = std::string(view["value"].get_utf8().value);
      return true;
    }
    return false;
  }

  bool MongoDBBackend::LookupParent(int64_t& parentId /*out*/, int64_t resourceId)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    bool res = false;
    auto doc = db["Resources"].find_one(make_document(kvp("internalId", resourceId)));

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
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto doc = db["Resources"].find_one(
      make_document(kvp("publicId", publicId))
    );

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
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto result = db["PatientRecyclingOrder"].find_one({});

    if (result)
    {
      internalId = result->view()["patientId"].get_int64().value;
      return true;
    }
    return false;
  }

  bool MongoDBBackend::SelectPatientToRecycle(int64_t& internalId /*out*/, int64_t patientIdToAvoid)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto result = db["PatientRecyclingOrder"].find_one(
      make_document(kvp("patientId", make_document(kvp("$ne", patientIdToAvoid)))),
      mongocxx::options::find{}.sort(make_document(kvp("id", 1)))
    );

    if (result)
    {
      internalId = result->view()["patientId"].get_int64().value;
      return true;
    }
    return false;
  }

  void MongoDBBackend::SetGlobalProperty(int32_t property, const char* value)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto pdocument = make_document(kvp("property", property), kvp("value", value));

    auto collection = db["GlobalProperties"];
    auto doc = collection.find_one_and_update(
      make_document(kvp("property", property)), pdocument.view()
    );

    if (!doc)
    {
      collection.insert_one(pdocument.view());
    }
  }

  void MongoDBBackend::SetMainDicomTag(int64_t id, uint16_t group, uint16_t element, const char* value)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["MainDicomTags"];
    auto main_dicom_document = make_document(
      kvp("id", id),
      kvp("tagGroup", group),
      kvp("tagElement", element),
      kvp("value", value)
    );

    collection.insert_one(main_dicom_document.view());
  }

  void MongoDBBackend::SetIdentifierTag(int64_t id, uint16_t group, uint16_t element, const char* value)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];
    bsoncxx::builder::stream::document document{};

    auto collection = db["DicomIdentifiers"];
    auto dicom_identifier_document = make_document(
      kvp("id", id),
      kvp("tagGroup", group),
      kvp("tagElement", element),
      kvp("value", value)
    );

    collection.insert_one(dicom_identifier_document.view());
  }

  void MongoDBBackend::SetMetadata(int64_t id, int32_t metadataType, const char* value)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["Metadata"];

    collection.delete_many(make_document(kvp("id", id), kvp("type", metadataType)));
   
    collection.insert_one(make_document(
      kvp("id", id), 
      kvp("type", metadataType), 
      kvp("value", value)
    ));
  }

  void MongoDBBackend::SetProtectedPatient(int64_t internalId, bool isProtected)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["PatientRecyclingOrder"];

    if (isProtected)
    {
      collection.delete_many(make_document(
        kvp("patientId", internalId)
      ));
    }
    else if (IsProtectedPatient(internalId))
    {
      int64_t seq = GetNextSequence(db, "PatientRecyclingOrder");
      collection.insert_one(make_document(
        kvp("id", seq),
        kvp("patientId", internalId)
      ));
    }
    else
    {
      // Nothing to do: The patient is already unprotected
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
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto delete_document = make_document(
        kvp("patientId", internalId)
    );

    db["MainDicomTags"].delete_many(delete_document.view());
    db["DicomIdentifiers"].delete_many(delete_document.view());
  }

  void MongoDBBackend::GetChildrenMetadata(std::list<std::string>& target,
                                     int64_t resourceId,
                                     int32_t metadata)
  {
    //SELECT internalId FROM Resources WHERE parentId=${id}
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::array;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto resCursor = db["Resources"].find(make_document(
        kvp("parentId", resourceId)
    ));

    //document with ids to lookup
    auto inCriteriaArray = array{};
    for (auto&& d : resCursor) {
      int64_t parentId = d["internalId"].get_int64().value;
      inCriteriaArray.append(parentId);
    }

    auto byIdValue = make_document(
      kvp("type", metadata),
      kvp("id", make_document(
        kvp("$in", inCriteriaArray.extract())
      ))
    );

    auto metadataCursor = db["Metadata"].find(byIdValue.view());
    for (auto&& doc : metadataCursor) {
      target.push_back(std::string(doc["value"].get_utf8().value));
    }
  }

  int64_t MongoDBBackend::GetLastChangeIndex()
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto seqDoc = db["Sequences"].find_one(make_document(kvp("name", "Changes")));

    if (seqDoc){
      return seqDoc->view()["i"].get_int64().value;
    } 

    else {
      return 0;
    }
  }

  void MongoDBBackend::TagMostRecentPatient(int64_t patientId)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["PatientRecyclingOrder"];
    auto recyclingOrderDoc = collection.find_one(make_document(kvp("patientId", patientId)));

    if (recyclingOrderDoc)
    {
        int64_t seq = recyclingOrderDoc->view()["id"].get_int64().value;
        collection.delete_many(make_document(kvp("id", seq)));

        // Refresh the patient "id" in list of unprotected patients
        seq = GetNextSequence(db, "PatientRecyclingOrder");
        collection.insert_one(make_document(
          kvp("id", seq), 
          kvp("patientId", patientId)
        ));
    }

  }

  bool MongoDBBackend::LookupResourceAndParent(int64_t& id, OrthancPluginResourceType& type, std::string& parentPublicId, const char* publicId)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    mongocxx::pipeline stages;
    auto match_stage = make_document(kvp("publicId", publicId));
    auto lookup_stage = make_document(
      kvp("from", "Resources"),
      kvp("foreignField", "internalId"),
      kvp("localField", "parentId" ),
      kvp("as", "parent")
    );
    auto unwind_stage = make_document(
      kvp("path", "$parent"), 
      kvp("preserveNullAndEmptyArrays", true)
    );
    auto group_stage = make_document(
      kvp("_id", bsoncxx::types::b_null()),
      kvp("internalId", make_document(kvp("$first", "$internalId"))),
      kvp("resourceType", make_document(kvp("$first", "$resourceType"))),
      kvp("publicId", make_document(kvp("$first", "$parent.publicId")))
    );

    stages.match(match_stage.view());
    stages.lookup(lookup_stage.view());
    stages.unwind(unwind_stage.view());
    stages.group(group_stage.view());
    stages.limit(1);

    auto cursor = db["Resources"].aggregate(stages);

    for (auto&& doc : cursor)
    {
      id = doc["internalId"].get_int64().value;
      type = static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value);

      bsoncxx::document::element publicId = doc["publicId"];

      if (publicId.type() == bsoncxx::type::k_utf8) {
        parentPublicId = std::string(publicId.get_utf8().value);
      }
      else {
        parentPublicId.clear();
      }

      return true;
    }

    return false;
  }

  void MongoDBBackend::GetAllMetadata(std::map<int32_t, std::string>& result, int64_t id)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto metadataCursor = db["Metadata"].find(make_document(kvp("id", id)));

    for (auto&& doc : metadataCursor)
    {
      result[doc["type"].get_int32().value] = std::string(doc["value"].get_utf8().value);
    }
  }

  void MongoDBBackend::LookupResources(
    const std::vector<OrthancPluginDatabaseConstraint>& lookup,
    OrthancPluginResourceType queryLevel, uint32_t limit, bool requestSomeInstance)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::array;
    using bsoncxx::builder::basic::sub_array;
    using bsoncxx::builder::basic::make_array;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto resourcesCollection = db["Resources"];

    auto normalStream = array{};
    auto identifierStream = array{};

    size_t normalCount = 0;
    size_t identifierCount = 0;

    std::map<std::string, bsoncxx::builder::basic::document> criterias;

    for (size_t i = 0; i < lookup.size(); i++) {
      OrthancPluginDatabaseConstraint constraint = lookup[i];

      auto case_sensitive_option = constraint.isCaseSensitive == 0 ? "i" : "";
      auto query_identifier = std::to_string(constraint.tagGroup) + 'x' + std::to_string(constraint.tagElement);

      if (criterias.find(query_identifier) == criterias.end()) {
        criterias[query_identifier] = std::move(bsoncxx::builder::basic::document{});
      }

      auto &current_document = criterias.at(query_identifier);

      switch (constraint.type)
      {
        case OrthancPluginConstraintType_Equal:
          current_document.append(
            kvp("$regex", constraint.values[0]),
            kvp("$options", case_sensitive_option)
          );
          break;

        case OrthancPluginConstraintType_SmallerOrEqual:
          current_document.append(
            kvp("$lte", constraint.values[0])
          );
          break;

        case OrthancPluginConstraintType_GreaterOrEqual:
          current_document.append(
            kvp("$gte", constraint.values[0])
          );
          break;

        case OrthancPluginConstraintType_List:
          current_document.append(
            kvp("$in", [constraint](sub_array child) {
              for (size_t i = 0; i < constraint.valuesCount; i++) {
                child.append(constraint.values[i]);
              }
            })
          );
          break;


        case OrthancPluginConstraintType_Wildcard:
          current_document.append(
            kvp("$regex", ConvertWildcardToRegex(constraint.values[0])),
            kvp("$options", case_sensitive_option)
          );
          break;

        default:
          throw MongoDBException("MongoDBBackend::LookupResources - invalid ConstraintType");
      }
    }

    for (size_t i = 0; i < lookup.size(); i++)
    {
      OrthancPluginDatabaseConstraint constraint = lookup[i];

      auto query_identifier = std::to_string(constraint.tagGroup) + 'x' + std::to_string(constraint.tagElement);
      auto current_document_query = criterias.find(query_identifier);

      if (current_document_query == criterias.end()) {
        continue;
      }

      bsoncxx::document::view_or_value criteria = make_document(
        kvp("tagGroup", constraint.tagGroup),
        kvp("tagElement" , constraint.tagElement),
        kvp("value" , current_document_query->second.extract())
      );

      if (constraint.isIdentifierTag == 1) {
        identifierCount++;
        identifierStream.append(criteria);
      }
      else {
        normalCount++;
        normalStream.append(criteria);
      }

      criterias.erase(current_document_query);  
    }

    mongocxx::pipeline stages;

    if (normalCount > 0 || identifierCount > 0) {
      auto limit_stage = 1;
      bsoncxx::builder::basic::document search_facet_stage{};

      if (normalCount > 0) {
        search_facet_stage.append(
          kvp("main_tags", make_array(
            make_document(
              kvp("$lookup", make_document(
                kvp("from", "MainDicomTags"),
                kvp("as", "tags"),
                kvp("pipeline", make_array(
                  make_document(
                    kvp("$match", make_document(
                      kvp("$or", normalStream.extract())
                    ))
                  )
                ))
              ))
            )
          ))
        );
      }

      if (identifierCount > 0) {
        search_facet_stage.append(
          kvp("identifier_tags", make_array(
            make_document(
              kvp("$lookup", make_document(
                kvp("from", "DicomIdentifiers"),
                kvp("as", "tags"),
                kvp("pipeline", make_array(
                  make_document(
                    kvp("$match", make_document(kvp("$or", identifierStream.extract())))
                  )
                ))
              ))
            )
          ))
        );
      }

      auto facet_field_project_stage = make_document(
        kvp("tags", make_document(
          kvp("$concatArrays", make_array(
            make_document(
              kvp("$ifNull", make_array(
                make_document(kvp("$arrayElemAt", make_array("$identifier_tags.tags", 0))), 
                make_array())
              )
            ),
            make_document(
              kvp("$ifNull", make_array(
                make_document(kvp("$arrayElemAt", make_array("$main_tags.tags", 0))),
                make_array())
              )
            )
          ))
        ))
      );

      auto unwind_stage = "$tags";
      auto replace_root_stage = make_document(kvp("newRoot", "$tags"));
      auto group_tags_stage = make_document(
        kvp("_id", "$id"), kvp("count", make_document(kvp("$sum", 1)))
      );

      auto resource_lookup_stage = make_document(
        kvp("from", "Resources"),
        kvp("as", "resources"),
        kvp("localField", "_id"),
        kvp("foreignField", "internalId")
      );

      auto resource_lookup_project_stage = make_document(
        kvp("count", 1),
        kvp("internalId", make_document(
          kvp("$arrayElemAt", make_array("$resources.internalId", 0))
        )),
        kvp("resourceType", make_document(
          kvp("$arrayElemAt", make_array("$resources.resourceType", 0))
        )),
        kvp("publicId", make_document(
          kvp("$arrayElemAt", make_array("$resources.publicId", 0))
        )),
        kvp("parentId", make_document(
          kvp("$arrayElemAt", make_array("$resources.parentId", 0))
        ))
      );

      auto resource_facet_stage = make_document(
        kvp("level", make_array(
          make_document(
            kvp("$match", make_document(kvp("resourceType", static_cast<int>(queryLevel))))
          ))
        ),
        kvp("children", make_array(
          make_document(kvp("$match", make_document(
            kvp("resourceType", make_document(kvp("$lt", static_cast<int>(queryLevel))))
          ))),
          make_document(kvp("$graphLookup", make_document(
            kvp("from", "Resources"),
            kvp("startWith", "$internalId"),
            kvp("connectFromField", "internalId"),
            kvp("connectToField", "parentId"),
            kvp("as", "children")
          ))),
          make_document(kvp("$unwind", "$children")),
          make_document(kvp("$replaceRoot", make_document(kvp("newRoot", "$children")))),
          make_document(kvp("$match", make_document(kvp("resourceType", static_cast<int>(queryLevel)))))
        )),
        kvp("parents", make_array(
          make_document(kvp("$match", make_document(
            kvp("resourceType", make_document(kvp("$gt", static_cast<int>(queryLevel))))
          ))),
          make_document(kvp("$graphLookup", make_document(
            kvp("from", "Resources"),
            kvp("startWith", "$parentId"),
            kvp("connectFromField", "parentId"),
            kvp("connectToField", "internalId"),
            kvp("as", "parents")
          ))),
          make_document(kvp("$unwind", "$parents")),
          make_document(kvp("$replaceRoot", make_document(kvp("newRoot", "$parents")))),
          make_document(kvp("$match", make_document(kvp("resourceType", static_cast<int>(queryLevel)))))
          )
        )
      );

      auto resources_add_field_stage = make_document(
        kvp("resources", make_document(
          kvp("$concatArrays", make_array(
            make_document(kvp("$ifNull", make_array("$level", make_array()))),
            make_document(kvp("$ifNull", make_array("$children", make_array()))),
            make_document(kvp("$ifNull", make_array("$parents", make_array())))
          ))
        ))
      );

      auto resource_replace_root_stage = make_document(
        kvp("newRoot", "$resources")
      );

      auto group_tags_resources_stage = make_document(
        kvp("_id", "$internalId"), 
        kvp("parentId", make_document(kvp("$first", "$parentId"))),
        kvp("internalId", make_document(kvp("$first", "$internalId"))),
        kvp("publicId", make_document(kvp("$first", "$publicId"))),
        kvp("count", make_document(kvp("$sum", 1)))
      );

      auto match_resources_stage = make_document(
        kvp("count", make_document(
          kvp("$gte", static_cast<int>(normalCount + identifierCount))
        ))
      );

      stages.limit(limit_stage);
      stages.facet(search_facet_stage.view());
      stages.project(facet_field_project_stage.view());
      stages.unwind(unwind_stage);
      stages.replace_root(replace_root_stage.view());
      stages.group(group_tags_stage.view());

      stages.lookup(resource_lookup_stage.view());
      stages.project(resource_lookup_project_stage.view());
      stages.facet(resource_facet_stage.view());
      stages.add_fields(resources_add_field_stage.view());
      stages.unwind("$resources");
      stages.replace_root(resource_replace_root_stage.view());
      
      stages.group(group_tags_resources_stage.view());
      stages.match(match_resources_stage.view());
    }
    else {
      auto match_resources_no_search_stage = make_document(
        kvp("resourceType", static_cast<int>(queryLevel))
      );

      stages.match(match_resources_no_search_stage.view());
    }


    // sort of the query by study or series
    if (queryLevel == OrthancPluginResourceType_Study || queryLevel == OrthancPluginResourceType_Series) {
      auto sort_build_lookup_pipe_stage = array{};

      if (queryLevel == OrthancPluginResourceType_Study) {
        sort_build_lookup_pipe_stage.append(
          make_document(kvp("tagGroup", 8), kvp("tagElement", 32)), // study_date
          make_document(kvp("tagGroup", 8), kvp("tagElement", 48)) // study_time
        );
      }

      if (queryLevel == OrthancPluginResourceType_Series) {
        sort_build_lookup_pipe_stage.append(
          make_document(kvp("tagGroup", 8), kvp("tagElement", 33)), // series_date
          make_document(kvp("tagGroup", 8), kvp("tagElement", 49)) // series_time
        );
      }

      auto sort_build_lookup_stage = make_document(
          kvp("as", "sorts"), 
          kvp("from", "MainDicomTags"),
          kvp("let", make_document(kvp("resource", "$internalId"))),
          kvp("pipeline", make_array(
            make_document(
              kvp("$match", make_document(
                kvp("$expr", make_document(kvp("$eq", make_array("$id", "$$resource")))),
                kvp("$or", sort_build_lookup_pipe_stage.extract())
              ))
            )
          ))  
      );

      auto sort_build_stage = make_document(
        kvp("sorts.0.value", -1), kvp("sorts.1.value", -1)
      );

      stages.lookup(sort_build_lookup_stage.view());
      stages.sort(sort_build_stage.view());
    }

    if (limit != 0) {
      stages.limit(limit);
    }

    if (requestSomeInstance) {
      stages.graph_lookup(
        make_document(
          kvp("from", "Resources"),
          kvp("startWith", "$internalId"),
          kvp("connectFromField", "internalId"),
          kvp("connectToField", "parentId"),
          kvp("as", "children")
        )
      );
      stages.unwind("$children");
      stages.match(
        make_document(kvp("children.resourceType", 3))
      );
      stages.group(
        make_document(
          kvp("_id", "$publicId"),
          kvp("instance_id", make_document(kvp("$first", "$children.publicId")))
        )
      );
    }

    auto cursor = resourcesCollection.aggregate(stages, mongocxx::options::aggregate{});

    for (auto&& doc : cursor) {
      if (requestSomeInstance) {
        GetOutput().AnswerMatchingResource(
          std::string(doc["_id"].get_utf8().value), 
          std::string(doc["instance_id"].get_utf8().value)
        );
      }
      else {
        GetOutput().AnswerMatchingResource(std::string(doc["publicId"].get_utf8().value));
      }
    }
  }

  void MongoDBBackend::SetResourcesContent(
    uint32_t countIdentifierTags, const OrthancPluginResourcesContentTags* identifierTags,
    uint32_t countMainDicomTags, const OrthancPluginResourcesContentTags* mainDicomTags,
    uint32_t countMetadata, const OrthancPluginResourcesContentMetadata* metadata)
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::array;
    using bsoncxx::builder::basic::make_document;
    
    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto metadataCollection = db["Metadata"];
    auto mainDicomTagsCollection = db["MainDicomTags"];
    auto dicomIdentifiersCollection = db["DicomIdentifiers"];

    std::vector< bsoncxx::document::value > dicomIdentifiersDocuments;
    std::vector< bsoncxx::document::value > mainDicomTagsDocuments;
    std::vector< bsoncxx::document::value > metadataDocuments;

    for (uint32_t i = 0; i < countIdentifierTags; i++)
    {
      dicomIdentifiersDocuments.push_back(
        make_document(
          kvp("id", identifierTags[i].resource),
          kvp("tagGroup", identifierTags[i].group),
          kvp("tagElement", identifierTags[i].element),
          kvp("value", identifierTags[i].value)
        )
      );
    }

    if (countIdentifierTags > 0) dicomIdentifiersCollection.insert_many(dicomIdentifiersDocuments);

    for (uint32_t i = 0; i < countMainDicomTags; i++)
    { 
      mainDicomTagsDocuments.push_back(
        make_document(
          kvp("id", mainDicomTags[i].resource),
          kvp("tagGroup", mainDicomTags[i].group),
          kvp("tagElement", mainDicomTags[i].element),
          kvp("value", mainDicomTags[i].value)
        )
      );
    }

    if (countMainDicomTags > 0) mainDicomTagsCollection.insert_many(mainDicomTagsDocuments);

    auto removeArray = array{};

    for (uint32_t i = 0; i < countMetadata; i++)
    {
      metadataDocuments.push_back(
        make_document(
          kvp("id", metadata[i].resource),
          kvp("type", metadata[i].metadata),
          kvp("value", metadata[i].value)
        )
      );

      removeArray.append(
        make_document(kvp("id",  metadata[i].resource), kvp("type", metadata[i].metadata))
      );
    }

    // just check for the $or not throw error
    if (countMetadata > 0) {
      metadataCollection.delete_many(make_document(kvp("$or", removeArray.extract())));
      metadataCollection.insert_many(metadataDocuments);
    }
  }

  void MongoDBBackend::CreateInstance(OrthancPluginCreateInstanceResult& result,
                                const char* hashPatient,
                                const char* hashStudy,
                                const char* hashSeries,
                                const char* hashInstance) 
  {
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto conn = pool_.acquire();
    auto db = (*conn)[dbname_];

    auto collection = db["Resources"];
    auto instance = collection.find_one(
      make_document(kvp("publicId",  hashInstance), kvp("resourceType", 3))
    );

    if (instance)
    {
      result.isNewInstance = false;
      result.instanceId = instance->view()["internalId"].get_int64().value;
    }
    else {
        auto patient = collection.find_one(
          make_document(kvp("publicId",  hashPatient), kvp("resourceType", 0))
        );
        auto study = collection.find_one(
          make_document(kvp("publicId",  hashStudy), kvp("resourceType", 1))
        );
        auto series = collection.find_one(
          make_document(kvp("publicId",  hashSeries), kvp("resourceType", 2))
        );

        if (patient) {
          result.isNewPatient = false;
          result.patientId = patient->view()["internalId"].get_int64().value;
        }
        else {
          if (study && series && instance) {
            throw MongoDBException("MongoDBBackend::CreateInstance - Broken invariant");
          }

          int64_t patientId = GetNextSequence(db, "Resources");
          auto patient_document = make_document(
            kvp("internalId", patientId), 
            kvp("resourceType", 0),
            kvp("publicId", hashPatient),
            kvp("parentId", bsoncxx::types::b_null())
          );

          collection.insert_one(patient_document.view());

          result.isNewPatient = true;
          result.patientId = patientId;
        }

        if (study) {
          result.isNewStudy = false;
          result.studyId = study->view()["internalId"].get_int64().value;
        }
        else {
          if (series && instance) {
            throw MongoDBException("MongoDBBackend::CreateInstance - Broken invariant");
          }

          int64_t studyId = GetNextSequence(db, "Resources");
          auto study_document = make_document(
            kvp("internalId", studyId), 
            kvp("resourceType", 1),
            kvp("publicId", hashStudy),
            kvp("parentId", result.patientId)
          );

          collection.insert_one(study_document.view());

          result.studyId = studyId;
          result.isNewStudy = true;
        }

        if (series) {
          result.isNewSeries = false;
          result.seriesId = series->view()["internalId"].get_int64().value;
        }
        else {
          if (instance) {
            throw MongoDBException("MongoDBBackend::CreateInstance - Broken invariant");
          }

          int64_t seriesId = GetNextSequence(db, "Resources");
          auto series_document = make_document(
            kvp("internalId", seriesId), 
            kvp("resourceType", 2),
            kvp("publicId", hashSeries),
            kvp("parentId", result.studyId)
          );

          collection.insert_one(series_document.view());

          result.seriesId = seriesId;
          result.isNewSeries = true;
        }

        int64_t instanceId = GetNextSequence(db, "Resources");
        auto instance_document = make_document(
          kvp("internalId", instanceId), 
          kvp("resourceType", 3),
          kvp("publicId", hashInstance),
          kvp("parentId", result.seriesId)
        );

        collection.insert_one(instance_document.view());

        result.isNewInstance = true;
        result.instanceId = instanceId;

        if (result.isNewPatient) {
          // add patient to PatientRecyclingOrder
          int64_t id = GetNextSequence(db, "PatientRecyclingOrder");
          db["PatientRecyclingOrder"].insert_one(make_document(
            kvp("id", id),
            kvp("patientId", result.patientId)
          ));
        }
        else {
          // update patient order in PatientRecyclingOrder
          TagMostRecentPatient(result.patientId);
        }
    }
  }
} //namespace OrthancPlugins
