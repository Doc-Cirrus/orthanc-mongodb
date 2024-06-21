/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2023  (Doc Cirrus GmbH)
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

#include "MongoDBIndex.h"

#include <bsoncxx/json.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/cursor.hpp>

#include "../../Framework/Plugins/GlobalProperties.h"
#include "../../Framework/MongoDB/MongoDatabase.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>
#include <OrthancException.h>

// mongocxx related
using bsoncxx::type;
using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::array;
using bsoncxx::builder::basic::sub_array;
using bsoncxx::builder::basic::make_array;
using bsoncxx::builder::basic::make_document;


namespace OrthancDatabases {
    static std::string ConvertWildcardToRegex(const std::string &query) {
        std::string s = "(?i)^";

        for (char i: query) {
            if (i == '*') {
                s += ".*";
            } else if (i == '.') {
                s += "\\.";
            } else if (i == '?') {
                s += '.';
            } else {
                s += i;
            }
        }
        s += "$";
        return s;
    }

    IDatabaseFactory *MongoDBIndex::CreateDatabaseFactory() {
        return MongoDatabase::CreateDatabaseFactory(url_, chunkSize_);
    }

    // protected
    // methods override for mongodb
    void MongoDBIndex::SignalDeletedFiles(
            IDatabaseBackendOutput &output,
            mongocxx::cursor &cursor
    ) {
        for (auto doc: cursor) {
            // auto v = doc.view();
            output.SignalDeletedAttachment(
                    std::string(doc["uuid"].get_string().value),
                    doc["fileType"].get_int32().value,
                    doc["uncompressedSize"].get_int64().value,
                    std::string(doc["uncompressedHash"].get_string().value),
                    doc["compressionType"].get_int32().value,
                    doc["compressedSize"].get_int64().value,
                    std::string(doc["compressedHash"].get_string().value)
            );
        }
    }

    void MongoDBIndex::SignalDeletedResources(
            IDatabaseBackendOutput &output,
            const std::vector<bsoncxx::document::view> deleted_resources_vec
    ) {
        for (auto &&doc: deleted_resources_vec) {
            output.SignalDeletedResource(
                    std::string(doc["publicId"].get_string().value),
                    static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value)
            );
        }
    }

    void MongoDBIndex::ConfigureDatabase(DatabaseManager &manager) {
        uint32_t expectedVersion = 6;

        if (GetContext())   // "GetContext()" can possibly be NULL in the unit tests
        {
            expectedVersion = OrthancPluginGetExpectedDatabaseVersion(GetContext());
        }

        // Check the expected version of the database
        if (expectedVersion != 6) {
            LOG(ERROR) << "This database plugin is incompatible with your version of Orthanc "
                       << "expecting the DB schema version " << expectedVersion
                       << ", but this plugin is only compatible with version 6";

            throw Orthanc::OrthancException(Orthanc::ErrorCode_Plugin);
        }

        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        if (!database.IsMaster()) {
            LOG(ERROR) << "MongoDB server is not master, could not write.";
            throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
        }

        {
            // indexes creation
            database.CreateIndices();
        }

        {
            SetGlobalIntegerProperty(
                    manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseSchemaVersion, expectedVersion
            );
            SetGlobalIntegerProperty(manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabasePatchLevel, 1);
        }

        {
            int version = 0;
            if (!LookupGlobalIntegerProperty(version, manager, MISSING_SERVER_IDENTIFIER,
                                             Orthanc::GlobalProperty_DatabaseSchemaVersion) || version != 6) {
                LOG(ERROR) << "MongoDB plugin is incompatible with database schema version: " << version;
                throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
            }

            int revision;
            if (!LookupGlobalIntegerProperty(revision, manager, MISSING_SERVER_IDENTIFIER,
                                             Orthanc::GlobalProperty_DatabasePatchLevel)) {
                revision = 1;
                SetGlobalIntegerProperty(manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabasePatchLevel,
                                         revision);
            }

            if (revision != 1) {
                LOG(ERROR) << "MongoDB plugin is incompatible with database schema revision: " << revision;
                throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
            }
        }
    }


    MongoDBIndex::MongoDBIndex(OrthancPluginContext *context, const std::string &url, const int &chunkSize) :
            IndexBackend(context), url_(url), chunkSize_(chunkSize) {
        if (url_.empty()) {
            throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
        }
    }


    MongoDBIndex::MongoDBIndex(OrthancPluginContext *context) :
            IndexBackend(context) {
    }

    void MongoDBIndex::AddAttachment(DatabaseManager &manager,
                                     int64_t id,
                                     const OrthancPluginAttachment &attachment,
                                     int64_t revision) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("AttachedFiles");

        auto attachment_document = make_document(
                kvp("id", id),
                kvp("fileType", attachment.contentType),
                kvp("uuid", attachment.uuid),
                kvp("compressedSize", static_cast<int64_t>(attachment.compressedSize)),
                kvp("uncompressedSize", static_cast<int64_t>(attachment.uncompressedSize)),
                kvp("compressionType", attachment.compressionType),
                kvp("uncompressedHash", attachment.uncompressedHash),
                kvp("compressedHash", attachment.compressedHash),
                kvp("revision", revision)
        );

        collection.insert_one(attachment_document.view());
    }

    void MongoDBIndex::AttachChild(DatabaseManager &manager,
                                   int64_t parent,
                                   int64_t child) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("Resources");

        collection.update_many(
                make_document(kvp("internalId", child)),
                make_document(kvp("$set", make_document(kvp("parentId", parent))))
        );
    }

    void MongoDBIndex::ClearChanges(DatabaseManager &manager) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("Changes");

        collection.delete_many({});
    }

    void MongoDBIndex::ClearExportedResources(DatabaseManager &manager) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("ExportedResources");

        collection.delete_many({});
    }

    void MongoDBIndex::DeleteAttachment(IDatabaseBackendOutput &output,
                                        DatabaseManager &manager,
                                        int64_t id,
                                        int32_t attachment) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        {
            auto collection = database.GetCollection("AttachedFiles");

            auto match = make_document(
                    kvp("id", static_cast<int64_t>(id)),
                    kvp("fileType", attachment)
            );

            mongocxx::pipeline stages;
            stages.match(match.view());

            auto attachedCursor = collection.aggregate(stages, mongocxx::options::aggregate{});

            collection.delete_many(match.view());
            SignalDeletedFiles(output, attachedCursor);
        }

    }

    void MongoDBIndex::DeleteMetadata(DatabaseManager &manager,
                                      int64_t id,
                                      int32_t metadataType) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("Metadata");

        collection.delete_many(make_document(
                kvp("id", static_cast<int64_t>(id)),
                kvp("type", metadataType)
        ));
    }

    void MongoDBIndex::DeleteResource(IDatabaseBackendOutput &output,
                                      DatabaseManager &manager,
                                      int64_t id) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto databaseInstance = database.GetObject();

        // resources collection
        auto collection = database.GetCollection(databaseInstance, "Resources");

        int64_t parent = -1;

        // remove resources
        {
            std::vector<bsoncxx::document::view> deleted_resources_vec;

            // find all resources to delete
            mongocxx::pipeline stages;
            auto resources_to_delete = array{};

            auto match_stage = make_document(kvp("internalId", id));
            auto add_field_stage = make_document(kvp("root", "$$ROOT"));
            auto graph_lookup_stage = make_document(
                    kvp("from", "Resources"),
                    kvp("startWith", "$internalId"),
                    kvp("connectFromField", "internalId"),
                    kvp("connectToField", "parentId"),
                    kvp("as", "children")
            );
            auto concat_fields_stage = make_document(
                    kvp("items", make_document(
                            kvp("$concatArrays", make_array(make_array("$root"), "$children"))
                    ))
            );
            auto unwind_stage = make_document(kvp("path", "$items"));
            auto replace_root_stage = make_document(kvp("newRoot", "$items"));

            stages.match(match_stage.view());
            stages.add_fields(add_field_stage.view());
            stages.graph_lookup(graph_lookup_stage.view());
            stages.add_fields(concat_fields_stage.view());
            stages.unwind(unwind_stage.view());
            stages.replace_root(replace_root_stage.view());

            auto deletedResourcesCursor = collection.aggregate(stages, mongocxx::options::aggregate{});

            for (auto &&doc: deletedResourcesCursor) {
                int64_t internalId = doc["internalId"].get_int64().value;
                int64_t parentId = doc["parentId"].type() == type::k_int64 ? doc["parentId"].get_int64().value : -1;

                parent = (internalId == id) ? parentId : parent;

                resources_to_delete.append(internalId);
                deleted_resources_vec.push_back(doc);
            }

            auto inCriteria = make_document(kvp("$in", resources_to_delete.extract()));
            auto byIdValue = make_document(kvp("id", inCriteria.view()));
            auto byPatientIdValue = make_document(kvp("patientId", inCriteria.view()));
            auto byInternalIdValue = make_document(kvp("internalId", inCriteria.view()));

            // files to delete
            auto attachedCursor = database.GetCollection(databaseInstance, "AttachedFiles").find(byIdValue.view());

            // Delete
            database.GetCollection(databaseInstance, "Metadata").delete_many(byIdValue.view());
            database.GetCollection(databaseInstance, "AttachedFiles").delete_many(byIdValue.view());
            database.GetCollection(databaseInstance, "Changes").delete_many(byInternalIdValue.view());
            database.GetCollection(databaseInstance, "PatientRecyclingOrder").delete_many(byPatientIdValue.view());
            database.GetCollection(databaseInstance, "MainDicomTags").delete_many(byIdValue.view());
            database.GetCollection(databaseInstance, "DicomIdentifiers").delete_many(byIdValue.view());
            collection.delete_many(byInternalIdValue.view());

            SignalDeletedFiles(output, attachedCursor);
            SignalDeletedResources(output, deleted_resources_vec);
        }

        // remain Ancestor
        if (parent != -1) {
            auto result = database.GetCollection("Resources").find_one(
                    make_document(kvp("internalId", parent))
            );

            if (result) {
                output.SignalRemainingAncestor(
                        std::string(result->view()["publicId"].get_string().value),
                        static_cast<OrthancPluginResourceType>(result->view()["resourceType"].get_int32().value)
                );
            }
        }
    }

    void MongoDBIndex::GetAllInternalIds(std::list<int64_t> &target,
                                         DatabaseManager &manager,
                                         OrthancPluginResourceType resourceType) {

        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        auto cursor = database.GetCollection("Resources").find(
                make_document(kvp("resourceType", static_cast<int>(resourceType)))
        );

        for (auto &&doc: cursor) {
            target.push_back(doc["internalId"].get_int64().value);
        }
    }

    void MongoDBIndex::GetAllPublicIds(std::list<std::string> &target,
                                       DatabaseManager &manager,
                                       OrthancPluginResourceType resourceType) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        auto cursor = database.GetCollection("Resources").find(
                make_document(kvp("resourceType", static_cast<int>(resourceType)))
        );

        for (auto &&doc: cursor) {
            target.emplace_back(doc["publicId"].get_string().value);
        }
    }

    void MongoDBIndex::GetAllPublicIds(std::list<std::string> &target,
                                       DatabaseManager &manager,
                                       OrthancPluginResourceType resourceType,
                                       uint64_t since,
                                       uint64_t limit) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        mongocxx::options::find options{};
        options.limit(limit).skip(since);

        auto cursor = database.GetCollection("Resources").find(
                make_document(kvp("resourceType", static_cast<int>(resourceType))), options
        );

        for (auto &&doc: cursor) {
            target.emplace_back(doc["publicId"].get_string().value);
        }
    }

    /* Use GetOutput().AnswerChange() */
    void MongoDBIndex::GetChanges(IDatabaseBackendOutput &output,
                                  bool &done /*out*/,
                                  DatabaseManager &manager,
                                  int64_t since,
                                  uint32_t maxResults) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        mongocxx::options::find options{};
        options.sort(make_document(kvp("seq", 1))).limit(maxResults + 1);

        done = true;
        uint32_t count = 0;

        auto cursor = database.GetCollection("Changes").find(
                make_document(kvp("id", make_document(kvp("$gt", since)))), options
        );

        for (auto &&doc: cursor) {
            if (count == maxResults) {
                done = false;
                break;
            }

            output.AnswerChange(
                    doc["id"].get_int64().value,
                    doc["changeType"].get_int32().value,
                    static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
                    GetPublicId(manager, doc["internalId"].get_int64().value),
                    std::string(doc["date"].get_string().value)
            );

            count++;
        }
    }

    void MongoDBIndex::GetChildrenInternalId(std::list<int64_t> &target /*out*/,
                                             DatabaseManager &manager,
                                             int64_t id) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        auto cursor = database.GetCollection("Resources").find(make_document(kvp("parentId", id)));

        for (auto &&doc: cursor) {
            target.emplace_back(doc["internalId"].get_int64().value);
        }
    }

    void MongoDBIndex::GetChildrenPublicId(std::list<std::string> &target /*out*/,
                                           DatabaseManager &manager,
                                           int64_t id) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        auto cursor = database.GetCollection("Resources").find(make_document(kvp("parentId", id)));

        for (auto &&doc: cursor) {
            target.emplace_back(doc["publicId"].get_string().value);
        }
    }

    /* Use GetOutput().AnswerExportedResource() */
    void MongoDBIndex::GetExportedResources(IDatabaseBackendOutput &output,
                                            bool &done /*out*/,
                                            DatabaseManager &manager,
                                            int64_t since,
                                            uint32_t maxResults) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        mongocxx::options::find options{};
        options.sort(make_document(kvp("id", 1))).limit(maxResults + 1);

        done = true;
        uint32_t count = 0;
        auto cursor = database.GetCollection("ExportedResources").find(
                make_document(kvp("id", make_document(kvp("$gt", since)))),
                options
        );

        for (auto &&doc: cursor) {
            if (count == maxResults) {
                done = false;
                break;
            }
            output.AnswerExportedResource(
                    doc["id"].get_int64().value,
                    static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
                    std::string(doc["publicId"].get_string().value),
                    std::string(doc["remoteModality"].get_string().value),
                    std::string(doc["date"].get_string().value),
                    std::string(doc["patientId"].get_string().value),
                    std::string(doc["studyInstanceUid"].get_string().value),
                    std::string(doc["seriesInstanceUid"].get_string().value),
                    std::string(doc["sopInstanceUid"].get_string().value)
            );

            count++;
        }
    }

    /* Use GetOutput().AnswerChange() */
    void MongoDBIndex::GetLastChange(IDatabaseBackendOutput &output,
                                     DatabaseManager &manager) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        mongocxx::options::find options{};
        options.sort(make_document(kvp("id", -1))).limit(1);

        auto cursor = database.GetCollection("Changes").find({}, options);

        for (auto &&doc: cursor) {
            output.AnswerChange(
                    doc["id"].get_int64().value,
                    doc["changeType"].get_int32().value,
                    static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
                    GetPublicId(manager, doc["internalId"].get_int64().value),
                    std::string(doc["date"].get_string().value)
            );
        }
    }

    /* Use GetOutput().AnswerExportedResource() */
    void MongoDBIndex::GetLastExportedResource(IDatabaseBackendOutput &output,
                                               DatabaseManager &manager) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        mongocxx::options::find options{};
        options.sort(make_document(kvp("id", -1))).limit(1);

        auto cursor = database.GetCollection("ExportedResources").find({}, options);
        for (auto &&doc: cursor) {
            output.AnswerExportedResource(
                    doc["id"].get_int64().value,
                    static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
                    std::string(doc["publicId"].get_string().value),
                    std::string(doc["remoteModality"].get_string().value),
                    std::string(doc["date"].get_string().value),
                    std::string(doc["patientId"].get_string().value),
                    std::string(doc["studyInstanceUid"].get_string().value),
                    std::string(doc["seriesInstanceUid"].get_string().value),
                    std::string(doc["sopInstanceUid"].get_string().value)
            );
        }
    }

    /* Use GetOutput().AnswerDicomTag() */
    void MongoDBIndex::GetMainDicomTags(IDatabaseBackendOutput &output,
                                        DatabaseManager &manager,
                                        int64_t id) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto cursor = database.GetCollection("MainDicomTags").find(make_document(kvp("id", id)));

        for (auto &&doc: cursor) {
            output.AnswerDicomTag(
                    static_cast<uint16_t>(doc["tagGroup"].get_int32().value),
                    static_cast<uint16_t>(doc["tagElement"].get_int32().value),
                    std::string(doc["value"].get_string().value)
            );
        }
    }

    std::string MongoDBIndex::GetPublicId(DatabaseManager &manager,
                                          int64_t resourceId) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto result = database.GetCollection("Resources").find_one(make_document(kvp("internalId", resourceId)));

        if (result) {
            return std::string(result->view()["publicId"].get_string().value);
        }
        throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }

    uint64_t MongoDBIndex::GetResourcesCount(DatabaseManager &manager,
                                             OrthancPluginResourceType resourceType) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        int64_t count = database.GetCollection("Resources").count_documents(
                make_document(kvp("resourceType", static_cast<int>(resourceType)))
        );

        return count;
    }

    OrthancPluginResourceType MongoDBIndex::GetResourceType(DatabaseManager &manager,
                                                            int64_t resourceId) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto result = database.GetCollection("Resources").find_one(make_document(kvp("internalId", resourceId)));

        if (result) {
            return static_cast<OrthancPluginResourceType>(result->view()["resourceType"].get_int32().value);
        }
        throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }

    uint64_t MongoDBIndex::GetTotalCompressedSize(DatabaseManager &manager) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        mongocxx::pipeline stages;
        auto group_stage = make_document(
                kvp("_id", bsoncxx::types::b_null()),
                kvp("totalSize", make_document(
                        kvp("$sum", "$compressedSize")
                ))
        );

        stages.group(group_stage.view());

        auto cursor = database.GetCollection("AttachedFiles").aggregate(stages);

        for (auto &&doc: cursor) {
            return doc["totalSize"].get_int64().value;
        }

        return 0;
    }

    uint64_t MongoDBIndex::GetTotalUncompressedSize(DatabaseManager &manager) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        mongocxx::pipeline stages;
        auto group_stage = make_document(
                kvp("_id", bsoncxx::types::b_null()),
                kvp("totalSize", make_document(
                        kvp("$sum", "$uncompressedSize")
                ))
        );

        stages.group(group_stage.view());

        auto cursor = database.GetCollection("AttachedFiles").aggregate(stages);

        for (auto &&doc: cursor) {
            return doc["totalSize"].get_int64().value;
        }

        return 0;
    }

    bool MongoDBIndex::IsExistingResource(DatabaseManager &manager,
                                          int64_t internalId) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        int64_t count = database.GetCollection("Resources").count_documents(
                make_document(kvp("internalId", internalId))
        );

        return count > 0;
    }

    bool MongoDBIndex::IsProtectedPatient(DatabaseManager &manager,
                                          int64_t internalId) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        int64_t count = database.GetCollection("PatientRecyclingOrder").count_documents(
                make_document(kvp("patientId", internalId))
        );

        return !count;
    }

    void MongoDBIndex::ListAvailableMetadata(std::list<int32_t> &target /*out*/,
                                             DatabaseManager &manager,
                                             int64_t id) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto cursor = database.GetCollection("Metadata").find(make_document(kvp("id", id)));

        for (auto &&doc: cursor) {
            target.push_back(doc["type"].get_int32().value);
        }
    }

    void MongoDBIndex::ListAvailableAttachments(std::list<int32_t> &target /*out*/,
                                                DatabaseManager &manager,
                                                int64_t id) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto cursor = database.GetCollection("AttachedFiles").find(make_document(kvp("id", id)));

        for (auto &&doc: cursor) {
            target.push_back(doc["fileType"].get_int32().value);
        }
    }

    void MongoDBIndex::LogChange(DatabaseManager &manager,
                                 int32_t changeType,
                                 int64_t resourceId,
                                 OrthancPluginResourceType resourceType,
                                 const char *date) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        int64_t seq = database.GetNextSequence("Changes");
        auto collection = database.GetCollection("Changes");

        auto change_document = make_document(
                kvp("id", seq),
                kvp("changeType", changeType),
                kvp("internalId", resourceId),
                kvp("resourceType", resourceType),
                kvp("date", date)
        );

        collection.insert_one(change_document.view());
    }

    void MongoDBIndex::LogExportedResource(DatabaseManager &manager, const OrthancPluginExportedResource &resource) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        int64_t seq = database.GetNextSequence("ExportedResources");
        auto collection = database.GetCollection("ExportedResources");

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
    bool MongoDBIndex::LookupAttachment(IDatabaseBackendOutput &output,
                                        int64_t &revision /*out*/,
                                        DatabaseManager &manager,
                                        int64_t id,
                                        int32_t contentType) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        auto doc = database.GetCollection("AttachedFiles").find_one(make_document(
                kvp("id", id), kvp("fileType", contentType)
        ));

        if (doc) {
            bsoncxx::document::view view = doc->view();

            output.AnswerAttachment(
                    std::string(view["uuid"].get_string().value),
                    contentType,
                    view["uncompressedSize"].get_int64().value,
                    std::string(view["uncompressedHash"].get_string().value),
                    view["compressionType"].get_int32().value,
                    view["compressedSize"].get_int64().value,
                    std::string(view["compressedHash"].get_string().value)
            );

            auto revisionElement = view["revision"];

            if (revisionElement && revisionElement.type() == type::k_int64) {
                revision = view["revision"].get_int64().value;
            } else revision = 0;

            return true;
        }

        return false;
    }

    bool MongoDBIndex::LookupGlobalProperty(std::string &target /*out*/,
                                            DatabaseManager &manager,
                                            const char *serverIdentifier,
                                            int32_t property) {
        // a hack for Orthanc's internal check
        if (property == Orthanc::GlobalProperty_DatabaseSchemaVersion) {
            target = std::string("6");
            return true;
        }

        if (serverIdentifier == nullptr) {
            throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
        } else {
            auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

            // try to enhance this one
            auto isServer = strlen(serverIdentifier) == 0;
            auto collection = database.GetCollection(isServer ? "GlobalProperties" : "ServerProperties");
            auto query = isServer ? make_document(kvp("property", property)) : make_document(
                    kvp("property", property), kvp("server", serverIdentifier)
            );

            auto document = collection.find_one(query.view());

            if (document) {
                target = std::string(document->view()["value"].get_string().value);
                return true;
            }
        }

        return false;
    }

    void MongoDBIndex::LookupIdentifier(std::list<int64_t> &target /*out*/,
                                        DatabaseManager &manager,
                                        OrthancPluginResourceType resourceType,
                                        uint16_t group,
                                        uint16_t element,
                                        OrthancPluginIdentifierConstraint constraint,
                                        const char *value) {
        bsoncxx::document::view_or_value criteria;

        switch (constraint) {
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
                throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
        }

        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto cursor = database.GetCollection("DicomIdentifiers").find(criteria.view());

        for (auto &&doc: cursor) {
            target.push_back(doc["id"].get_int64().value);
        }
    }

    void MongoDBIndex::LookupIdentifierRange(std::list<int64_t> &target /*out*/,
                                             DatabaseManager &manager,
                                             OrthancPluginResourceType resourceType,
                                             uint16_t group,
                                             uint16_t element,
                                             const char *start,
                                             const char *end) {
        auto criteria = make_document(
                kvp("tagGroup", group),
                kvp("tagElement", element),
                kvp("value", make_document(
                        kvp("$gte", start), kvp("$lte", end)
                ))
        );

        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto cursor = database.GetCollection("DicomIdentifiers").find(criteria.view());

        for (auto &&doc: cursor) {
            target.push_back(doc["id"].get_int64().value);
        }
    }

    bool MongoDBIndex::LookupMetadata(std::string &target /*out*/,
                                      int64_t &revision /*out*/,
                                      DatabaseManager &manager,
                                      int64_t id,
                                      int32_t metadataType) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto doc = database.GetCollection("Metadata").find_one(make_document(kvp("id", id), kvp("type", metadataType)));

        if (doc) {
            bsoncxx::document::view view = doc->view();
            target = std::string(view["value"].get_string().value);

            auto revisionElement = view["revision"];

            if (revisionElement && revisionElement.type() == type::k_int64) {
                revision = view["revision"].get_int64().value;
            } else revision = 0;

            return true;
        }
        return false;
    }

    bool MongoDBIndex::LookupParent(int64_t &parentId /*out*/,
                                    DatabaseManager &manager,
                                    int64_t resourceId) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        bool res = false;
        auto doc = database.GetCollection("Resources").find_one(make_document(kvp("internalId", resourceId)));

        if (doc) {
            bsoncxx::document::element parent = doc->view()["parentId"];

            if (parent && parent.type() == type::k_int64) {
                parentId = parent.get_int64().value;
                res = true;
            }
        }
        return res;
    }

    bool MongoDBIndex::LookupResource(int64_t &id /*out*/,
                                      OrthancPluginResourceType &type /*out*/,
                                      DatabaseManager &manager,
                                      const char *publicId) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto doc = database.GetCollection("Resources").find_one(
                make_document(kvp("publicId", publicId))
        );

        if (doc) {
            bsoncxx::document::view view = doc->view();
            id = view["internalId"].get_int64().value;
            type = static_cast<OrthancPluginResourceType>(view["resourceType"].get_int32().value);
            return true;
        }
        return false;
    }

    bool MongoDBIndex::SelectPatientToRecycle(int64_t &internalId /*out*/,
                                              DatabaseManager &manager) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto result = database.GetCollection("PatientRecyclingOrder").find_one({});

        if (result) {
            internalId = result->view()["patientId"].get_int64().value;
            return true;
        }
        return false;
    }

    bool MongoDBIndex::SelectPatientToRecycle(int64_t &internalId /*out*/,
                                              DatabaseManager &manager,
                                              int64_t patientIdToAvoid) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto result = database.GetCollection("PatientRecyclingOrder").find_one(
                make_document(kvp("patientId", make_document(kvp("$ne", patientIdToAvoid)))),
                mongocxx::options::find{}.sort(make_document(kvp("id", 1)))
        );

        if (result) {
            internalId = result->view()["patientId"].get_int64().value;
            return true;
        }

        return false;
    }

    void MongoDBIndex::SetGlobalProperty(DatabaseManager &manager,
                                         const char *serverIdentifier,
                                         int32_t property,
                                         const char *utf8) {
        if (serverIdentifier == nullptr) {
            throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
        }

        bool hasServer = (strlen(serverIdentifier) != 0);
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        auto collection = database.GetCollection(hasServer ? "ServerProperties" : "GlobalProperties");
        auto query = make_document(kvp("property", property));
        auto pDocument = make_document(kvp("property", property), kvp("value", utf8));

        if (hasServer) {
            query.reset(make_document(kvp("property", property), kvp("server", serverIdentifier)));
            pDocument.reset(
                    make_document(kvp("property", property), kvp("value", utf8), kvp("server", serverIdentifier))
            );
        }

        auto doc = collection.find_one_and_update(
                query.view(), pDocument.view()
        );

        if (!doc) {
            collection.insert_one(pDocument.view());
        }
    }

    void MongoDBIndex::SetMainDicomTag(DatabaseManager &manager,
                                       int64_t id,
                                       uint16_t group,
                                       uint16_t element,
                                       const char *value) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("MainDicomTags");

        auto main_dicom_document = make_document(
                kvp("id", id),
                kvp("tagGroup", group),
                kvp("tagElement", element),
                kvp("value", value)
        );

        collection.insert_one(main_dicom_document.view());
    }

    void MongoDBIndex::SetIdentifierTag(DatabaseManager &manager,
                                        int64_t id,
                                        uint16_t group,
                                        uint16_t element,
                                        const char *value) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("DicomIdentifiers");

        auto dicom_identifier_document = make_document(
                kvp("id", id),
                kvp("tagGroup", group),
                kvp("tagElement", element),
                kvp("value", value)
        );

        collection.insert_one(dicom_identifier_document.view());
    }

    void MongoDBIndex::SetMetadata(DatabaseManager &manager,
                                   int64_t id,
                                   int32_t metadataType,
                                   const char *value,
                                   int64_t revision) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("Metadata");

        mongocxx::options::bulk_write options;
        options.ordered(true);
        auto bulk = collection.create_bulk_write(options);

        auto deleteDocument = make_document(kvp("id", id), kvp("type", metadataType));
        auto insertDocument = make_document(
                kvp("id", id),
                kvp("type", metadataType),
                kvp("value", value),
                kvp("revision", revision)
        );

        mongocxx::model::delete_many delete_op{deleteDocument.view()};
        mongocxx::model::insert_one insert_op{insertDocument.view()};

        bulk.append(delete_op);
        bulk.append(insert_op);

        bulk.execute();
    }

    void MongoDBIndex::SetProtectedPatient(DatabaseManager &manager,
                                           int64_t internalId,
                                           bool isProtected) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto collection = database.GetCollection("PatientRecyclingOrder");

        if (isProtected) {
            collection.delete_many(make_document(
                    kvp("patientId", internalId)
            ));
        } else if (IsProtectedPatient(manager, internalId)) {
            int64_t seq = database.GetNextSequence("PatientRecyclingOrder");
            collection.insert_one(make_document(
                    kvp("id", seq),
                    kvp("patientId", internalId)
            ));
        } else {
            // Nothing to do: The patient is already unprotected
        }
    }

    void MongoDBIndex::ClearMainDicomTags(DatabaseManager &manager,
                                          int64_t internalId) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto databaseInstance = database.GetObject();

        auto delete_document = make_document(
                kvp("id", internalId)
        );

        database.GetCollection(databaseInstance, "MainDicomTags").delete_many(delete_document.view());
        database.GetCollection(databaseInstance, "DicomIdentifiers").delete_many(delete_document.view());
    }

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1

    static bool sortConstraintLookup(Orthanc::DatabaseConstraint left, Orthanc::DatabaseConstraint right) {
        return (left.GetLevel() > right.GetLevel());
    }

    // New primitive since Orthanc 1.5.2
    void MongoDBIndex::LookupResources(IDatabaseBackendOutput &output,
                                       DatabaseManager &manager,
                                       const std::vector<Orthanc::DatabaseConstraint> &lookup,
                                       OrthancPluginResourceType queryLevel,
                                       uint32_t limit,
                                       bool requestSomeInstance) {
                                        
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto databaseInstance = database.GetObject();

        auto collection = database.GetCollection(databaseInstance, "Resources");

        auto normalStream = array{};
        auto identifierStream = array{};
        std::vector<std::string> normalLevels;

        size_t normalCount = 0;
        size_t identifierCount = 0;
        bool identifierExact = false;

        // copy the passed lookup then sort it
        std::vector<Orthanc::DatabaseConstraint> lookup_sorted = lookup;
        std::sort(std::begin(lookup_sorted), std::end(lookup_sorted), sortConstraintLookup);

        std::map<std::string, bsoncxx::builder::basic::document> criterias;

        for (const auto& constraint: lookup_sorted) {
            // auto case_sensitive_option = constraint.isCaseSensitive ? "" : "i";
            auto query_identifier = std::to_string(constraint.GetTag().GetGroup()) + 'x' +
                                    std::to_string(constraint.GetTag().GetElement());

            if (identifierExact && constraint.GetConstraintType() == Orthanc::ConstraintType_Equal &&
                constraint.IsIdentifier())
                break;
            if (constraint.GetConstraintType() == Orthanc::ConstraintType_Equal && constraint.IsIdentifier()) {
                identifierExact = true;
            }

            if (criterias.find(query_identifier) == criterias.end()) {
                criterias[query_identifier] = std::move(bsoncxx::builder::basic::document{});
            }

            auto &current_document = criterias.at(query_identifier);

            switch (constraint.GetConstraintType()) {
                case Orthanc::ConstraintType_Equal:
                    current_document.append(
                            kvp("$eq", constraint.GetSingleValue())
                    );
                    /* TODO see what to do with slow regex issue
                    kvp("$regex", constraint.GetSingleValue()
                    kvp("$options", case_sensitive_option)
                    */
                    break;

                case Orthanc::ConstraintType_SmallerOrEqual:
                    current_document.append(
                            kvp("$lte", constraint.GetSingleValue())
                    );
                    break;

                case Orthanc::ConstraintType_GreaterOrEqual:
                    current_document.append(
                            kvp("$gte", constraint.GetSingleValue())
                    );
                    break;

                case Orthanc::ConstraintType_List:
                    current_document.append(
                            kvp("$in", [constraint](sub_array child) {
                                for (size_t i = 0; i < constraint.GetValuesCount(); i++) {
                                    child.append(constraint.GetValue(i));
                                }
                            })
                    );
                    break;


                case Orthanc::ConstraintType_Wildcard:
                    if (constraint.GetSingleValue() != "*") {
                        current_document.append(
                                kvp("$regex", ConvertWildcardToRegex(constraint.GetSingleValue()))
                        );
                    }

                    break;

                default:
                    throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
            }
        }

        for (const auto& constraint: lookup_sorted) {
            auto query_identifier = std::to_string(constraint.GetTag().GetGroup()) + 'x' +
                                    std::to_string(constraint.GetTag().GetElement());
            auto current_document_query = criterias.find(query_identifier);

            if (current_document_query == criterias.end()) {
                continue;
            }

            bsoncxx::document::view_or_value criteria = make_document(
                    kvp("tagGroup", constraint.GetTag().GetGroup()),
                    kvp("tagElement", constraint.GetTag().GetElement()),
                    kvp("value", current_document_query->second.extract())
            );

            if (constraint.IsIdentifier()) {
                identifierCount++;
                identifierStream.append(criteria);
            } else {
                normalCount++;
                normalStream.append(criteria);

                auto levelAttr = "$" + std::to_string(constraint.GetLevel());
                if (std::find(normalLevels.begin(), normalLevels.end(), levelAttr) == normalLevels.end()) {
                    normalLevels.push_back(levelAttr);
                }
            }

            criterias.erase(current_document_query);
        }

        mongocxx::pipeline stages;

        if (normalCount > 0 && identifierCount == 0) {
            auto normal_match_stage = make_document(
                    kvp("$or", normalStream.extract())
            );

            stages.match(normal_match_stage.view());
            collection = database.GetCollection(databaseInstance, "MainDicomTags");
        } else if (normalCount == 0 && identifierCount > 0) {
            auto identifier_match_stage = make_document(
                    kvp("$or", identifierStream.extract())
            );

            stages.match(identifier_match_stage.view());
            collection = database.GetCollection(databaseInstance, "DicomIdentifiers");
        } else if (normalCount == 0 && identifierCount == 0) {
            auto match_resources_no_search_stage = make_document(
                    kvp("resourceType", static_cast<int>(queryLevel))
            );

            stages.match(match_resources_no_search_stage.view());
        } else if (normalCount > 0 && identifierCount > 0) {
            mongocxx::pipeline identifiers_stages;

            auto identifier_match_stage = make_document(
                    kvp("$or", identifierStream.extract())
            );

            auto graph_lookup_stage = make_document(
                    kvp("as", "resources"),
                    kvp("startWith", "$id"),
                    kvp("from", "Resources"),
                    kvp("connectToField", "internalId"),
                    kvp("connectFromField", std::to_string(queryLevel))
            );

            auto resource_replace_root_stage = make_document(
                    kvp("newRoot", "$resources")
            );

            auto project_stage = make_document(
                    kvp("_id", 1), kvp("resources", make_document(
                            kvp("$concatArrays", [normalLevels](sub_array child) {
                                for (const auto &normalLevel: normalLevels) {
                                    child.append(normalLevel);
                                }
                            }))
                    )
            );

            auto group_stage = make_document(
                    kvp("_id", "$resources")
            );

            identifiers_stages.match(identifier_match_stage.view());
            identifiers_stages.graph_lookup(graph_lookup_stage.view());
            identifiers_stages.unwind("$resources");
            identifiers_stages.replace_root(resource_replace_root_stage.view());
            identifiers_stages.project(project_stage.view());
            identifiers_stages.unwind("$resources");
            identifiers_stages.group(group_stage.view());

            auto main_tags_ids = array{};
            mongocxx::options::aggregate identifiersAggregateOptions{};
            identifiersAggregateOptions.allow_disk_use(true);

            auto identifier_cursor = database.GetCollection(databaseInstance, "DicomIdentifiers").aggregate(
                    identifiers_stages, identifiersAggregateOptions
            );

            for (auto &&doc: identifier_cursor) {
                main_tags_ids.append(doc["_id"].get_int64().value);
            }

            auto normal_pre_match_stage = make_document(
                    kvp("id", make_document(kvp("$in", main_tags_ids.extract())))
            );

            auto normal_match_stage = make_document(
                    kvp("$or", normalStream.extract())
            );

            stages.match(normal_pre_match_stage.view());
            stages.match(normal_match_stage.view());

            collection = database.GetCollection(databaseInstance, "MainDicomTags");
        }

        if (normalCount > 0 || identifierCount > 0) {
            auto graph_lookup_stage = make_document(
                    kvp("as", "resources"),
                    kvp("startWith", "$id"),
                    kvp("from", "Resources"),
                    kvp("connectToField", "internalId"),
                    kvp("connectFromField", std::to_string(queryLevel))
            );

            auto resource_replace_root_stage = make_document(
                    kvp("newRoot", "$resources")
            );

            auto match_resources_stage = make_document(
                    kvp("resourceType", queryLevel)
            );

            stages.graph_lookup(graph_lookup_stage.view());
            stages.unwind("$resources");
            stages.replace_root(resource_replace_root_stage.view());
            stages.match(match_resources_stage.view());
        }

        // final stages
        auto group_resources = make_document(
                kvp("_id", "$internalId"),
                kvp("item", make_document(kvp("$first", "$$ROOT")))
        );
        auto replace_root = make_document(kvp("newRoot", "$item"));

        stages.group(group_resources.view());
        stages.replace_root(replace_root.view());

        // sort of the query by study or series
        if (queryLevel == OrthancPluginResourceType_Study || queryLevel == OrthancPluginResourceType_Series) {
            auto sort_build_stage = make_document(
                    kvp("sorts.0", -1), kvp("sorts.1", -1)
            );

            stages.sort(sort_build_stage.view());
        }

        if (limit != 0) {
            stages.limit(static_cast<int>(limit) - 1);
        }

        mongocxx::options::aggregate aggregateOptions{};
        aggregateOptions.allow_disk_use(true);

        auto cursor = collection.aggregate(stages, aggregateOptions);

        for (auto &&doc: cursor) {
            if (requestSomeInstance) {
                output.AnswerMatchingResource(
                        std::string(doc["publicId"].get_string().value),
                        std::string(doc["instancePublicId"].get_string().value)
                );
            } else {
                output.AnswerMatchingResource(std::string(doc["publicId"].get_string().value));
            }
        }
    }

#endif
#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1

    //  helpers functions
    static void ExecuteSetResourcesContentTags(DatabaseManager &manager, const std::string &collectionName,
                                               uint32_t count,
                                               const OrthancPluginResourcesContentTags *tags) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto databaseInstance = database.GetObject();

        auto collection = database.GetCollection(databaseInstance, collectionName);
        auto resourceCollection = database.GetCollection(databaseInstance, "Resources");
        auto bulk = collection.create_bulk_write();

        for (uint32_t i = 0; i < count; i++) {
            auto doc = make_document(
                    kvp("id", tags[i].resource),
                    kvp("tagGroup", tags[i].group),
                    kvp("tagElement", tags[i].element),
                    kvp("value", tags[i].value)
            );

            mongocxx::model::insert_one insert_op{doc.view()};
            bulk.append(insert_op);

            // study and series date and time for sorting
            if (collectionName == "MainDicomTags" && tags[i].group == 8 &&
                (tags[i].element == 32 || tags[i].element == 48 || tags[i].element == 33 || tags[i].element == 49)) {
                auto resource_find = make_document(kvp("internalId", tags[i].resource));
                auto resource_update = make_document(
                        kvp("$addToSet", make_document(kvp("sorts", tags[i].value)))
                );

                resourceCollection.update_one(resource_find.view(), resource_update.view());
            }
        }

        if (count > 0) bulk.execute();
    }

    static void ExecuteSetResourcesContentMetadata(DatabaseManager &manager, const std::string &collectionName,
                                                   uint32_t count,
                                                   const OrthancPluginResourcesContentMetadata *meta) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto databaseInstance = database.GetObject();

        auto collection = database.GetCollection(databaseInstance, collectionName);
        auto bulk = collection.create_bulk_write();

        auto removeArray = array{};
        std::vector<bsoncxx::document::value> metaDocuments;

        for (uint32_t i = 0; i < count; i++) {
            auto insertDoc = make_document(
                    kvp("id", meta[i].resource),
                    kvp("type", meta[i].metadata),
                    kvp("value", meta[i].value)
            );

            auto removeDoc = make_document(
                    kvp("id", meta[i].resource),
                    kvp("type", meta[i].metadata)
            );

            mongocxx::model::delete_one delete_one{removeDoc.view()};
            mongocxx::model::insert_one insert_op{insertDoc.view()};

            bulk.append(delete_one);
            bulk.append(insert_op);
        }

        // just check for the $or not throw error
        if (count > 0) bulk.execute();
    }

    // New primitive since Orthanc 1.5.2
    void MongoDBIndex::SetResourcesContent(
            DatabaseManager &manager,
            uint32_t countIdentifierTags,
            const OrthancPluginResourcesContentTags *identifierTags,
            uint32_t countMainDicomTags,
            const OrthancPluginResourcesContentTags *mainDicomTags,
            uint32_t countMetadata,
            const OrthancPluginResourcesContentMetadata *metadata) {
        ExecuteSetResourcesContentTags(manager, "DicomIdentifiers", countIdentifierTags, identifierTags);
        ExecuteSetResourcesContentTags(manager, "MainDicomTags", countMainDicomTags, mainDicomTags);
        ExecuteSetResourcesContentMetadata(manager, "Metadata", countMetadata, metadata);
    }

#endif

// New primitive since Orthanc 1.5.2
    void MongoDBIndex::GetChildrenMetadata(std::list<std::string> &target,
                                           DatabaseManager &manager,
                                           int64_t resourceId,
                                           int32_t metadata) {
        //SELECT internalId FROM Resources WHERE parentId=${id}
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto databaseInstance = database.GetObject();

        auto resCursor = database.GetCollection(databaseInstance, "Resources").find(make_document(
                kvp("parentId", resourceId)
        ));

        //document with ids to lookup
        auto inCriteriaArray = array{};
        for (auto &&d: resCursor) {
            int64_t parentId = d["internalId"].get_int64().value;
            inCriteriaArray.append(parentId);
        }

        auto byIdValue = make_document(
                kvp("type", metadata),
                kvp("id", make_document(
                        kvp("$in", inCriteriaArray.extract())
                ))
        );

        auto metadataCursor = database.GetCollection(databaseInstance, "Metadata").find(byIdValue.view());
        for (auto &&doc: metadataCursor) {
            target.emplace_back(doc["value"].get_string().value);
        }
    }

    // New primitive since Orthanc 1.5.2
    void MongoDBIndex::TagMostRecentPatient(DatabaseManager &manager,
                                            int64_t patient) {

        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        auto collection = database.GetCollection("PatientRecyclingOrder");
        auto recyclingOrderDoc = collection.find_one(make_document(kvp("patientId", patient)));

        if (recyclingOrderDoc) {
            int64_t seq = recyclingOrderDoc->view()["id"].get_int64().value;
            collection.delete_many(make_document(kvp("id", seq)));

            // Refresh the patient "id" in list of unprotected patients
            seq = database.GetNextSequence("PatientRecyclingOrder");
            collection.insert_one(make_document(
                    kvp("id", seq),
                    kvp("patientId", patient)
            ));
        }
    }

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)

    // New primitive since Orthanc 1.5.4
    bool MongoDBIndex::LookupResourceAndParent(int64_t &id,
                                               OrthancPluginResourceType &type,
                                               std::string &parentPublicId,
                                               DatabaseManager &manager,
                                               const char *publicId) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        mongocxx::pipeline stages;
        auto match_stage = make_document(kvp("publicId", publicId));

        auto lookup_stage = make_document(
                kvp("from", "Resources"),
                kvp("foreignField", "internalId"),
                kvp("localField", "parentId"),
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

        auto cursor = database.GetCollection("Resources").aggregate(stages);

        for (auto &&doc: cursor) {
            id = doc["internalId"].get_int64().value;
            type = static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value);

            bsoncxx::document::element publicIdElement = doc["publicId"];

            if (publicIdElement.type() == bsoncxx::type::k_utf8) {
                parentPublicId = std::string(publicIdElement.get_string().value);
            } else {
                parentPublicId.clear();
            }

            return true;
        }

        return false;
    }

#  endif
#endif

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)

    // New primitive since Orthanc 1.5.4
    void MongoDBIndex::GetAllMetadata(std::map<int32_t, std::string> &result,
                                      DatabaseManager &manager,
                                      int64_t id) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());
        auto metadataCursor = database.GetCollection("Metadata").find(make_document(kvp("id", id)));

        for (auto &&doc: metadataCursor) {
            result[doc["type"].get_int32().value] = std::string(doc["value"].get_string().value);
        }
    }

#  endif
#endif
#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1

    void MongoDBIndex::CreateInstance(OrthancPluginCreateInstanceResult &result,
                                      DatabaseManager &manager,
                                      const char *hashPatient,
                                      const char *hashStudy,
                                      const char *hashSeries,
                                      const char *hashInstance) {
        auto &database = dynamic_cast<MongoDatabase &>(manager.GetDatabase());

        auto collection = database.GetCollection("Resources");
        auto instance = collection.find_one(
                make_document(kvp("publicId", hashInstance), kvp("resourceType", 3))
        );

        if (instance) {
            result.isNewInstance = false;
            result.instanceId = instance->view()["internalId"].get_int64().value;
        } else {
            auto bulk = collection.create_bulk_write();

            auto patient = collection.find_one(
                    make_document(kvp("publicId", hashPatient), kvp("resourceType", 0))
            );
            auto study = collection.find_one(
                    make_document(kvp("publicId", hashStudy), kvp("resourceType", 1))
            );
            auto series = collection.find_one(
                    make_document(kvp("publicId", hashSeries), kvp("resourceType", 2))
            );

            if (patient) {
                result.isNewPatient = false;
                result.patientId = patient->view()["internalId"].get_int64().value;
            } else {
                if (study && series && instance) {
                    throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
                }

                int64_t patientId = database.GetNextSequence("Resources");
                auto patient_document = make_document(
                        kvp("internalId", patientId),
                        kvp("resourceType", 0),
                        kvp("publicId", hashPatient),
                        kvp("parentId", bsoncxx::types::b_null()),

                        kvp("0", make_array(patientId)),
                        kvp("1", make_array()),
                        kvp("2", make_array()),
                        kvp("3", make_array()),

                        kvp("instancePublicId", hashInstance)
                );

                mongocxx::model::insert_one insert_patient{patient_document.view()};
                bulk.append(insert_patient);

                result.isNewPatient = true;
                result.patientId = patientId;
            }

            if (study) {
                result.isNewStudy = false;
                result.studyId = study->view()["internalId"].get_int64().value;
            } else {
                if (series && instance) {
                    throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
                }

                int64_t studyId = database.GetNextSequence("Resources");
                auto study_document = make_document(
                        kvp("internalId", studyId),
                        kvp("resourceType", 1),
                        kvp("publicId", hashStudy),
                        kvp("parentId", result.patientId),

                        kvp("0", make_array(result.patientId)),
                        kvp("1", make_array(studyId)),
                        kvp("2", make_array()),
                        kvp("3", make_array()),

                        kvp("sorts", make_array()),
                        kvp("instancePublicId", hashInstance)
                );

                mongocxx::model::insert_one insert_study{study_document.view()};
                bulk.append(insert_study);

                result.studyId = studyId;
                result.isNewStudy = true;
            }

            if (series) {
                result.isNewSeries = false;
                result.seriesId = series->view()["internalId"].get_int64().value;
            } else {
                if (instance) {
                    throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
                }

                int64_t seriesId = database.GetNextSequence("Resources");
                auto series_document = make_document(
                        kvp("internalId", seriesId),
                        kvp("resourceType", 2),
                        kvp("publicId", hashSeries),
                        kvp("parentId", result.studyId),

                        kvp("0", make_array(result.patientId)),
                        kvp("1", make_array(result.studyId)),
                        kvp("2", make_array(seriesId)),
                        kvp("3", make_array()),

                        kvp("sorts", make_array()),
                        kvp("instancePublicId", hashInstance)
                );

                mongocxx::model::insert_one insert_series{series_document.view()};
                bulk.append(insert_series);

                result.seriesId = seriesId;
                result.isNewSeries = true;
            }

            int64_t instanceId = database.GetNextSequence("Resources");
            auto instance_document = make_document(
                    kvp("internalId", instanceId),
                    kvp("resourceType", 3),
                    kvp("publicId", hashInstance),
                    kvp("parentId", result.seriesId),

                    kvp("0", make_array(result.patientId)),
                    kvp("1", make_array(result.studyId)),
                    kvp("2", make_array(result.seriesId)),
                    kvp("3", make_array(instanceId)),

                    kvp("instancePublicId", hashInstance)
            );

            mongocxx::model::insert_one insert_instance{instance_document.view()};
            bulk.append(insert_instance);

            result.isNewInstance = true;
            result.instanceId = instanceId;

            if (result.patientId) {
                auto find_patient_document = make_document(kvp("internalId", result.patientId));
                auto update_patient_document = make_document(
                        kvp("$addToSet", make_document(
                                kvp("0", result.patientId),
                                kvp("1", result.studyId),
                                kvp("2", result.seriesId),
                                kvp("3", result.instanceId)
                        ))
                );

                mongocxx::model::update_one update_patient{
                        find_patient_document.view(), update_patient_document.view()
                };

                bulk.append(update_patient);
            }

            if (result.studyId) {
                auto find_study_document = make_document(kvp("internalId", result.studyId));
                auto update_study_document = make_document(
                        kvp("$addToSet", make_document(
                                kvp("0", result.patientId),
                                kvp("1", result.studyId),
                                kvp("2", result.seriesId),
                                kvp("3", result.instanceId)
                        ))
                );

                mongocxx::model::update_one update_study{find_study_document.view(), update_study_document.view()};
                bulk.append(update_study);
            }

            if (result.seriesId) {
                auto find_series_document = make_document(kvp("internalId", result.seriesId));
                auto update_series_document = make_document(
                        kvp("$addToSet", make_document(
                                kvp("0", result.patientId),
                                kvp("1", result.studyId),
                                kvp("2", result.seriesId),
                                kvp("3", result.instanceId)
                        ))
                );

                mongocxx::model::update_one update_series{find_series_document.view(), update_series_document.view()};
                bulk.append(update_series);
            }

            bulk.execute();

            if (result.isNewPatient) {
                // add patient to PatientRecyclingOrder
                int64_t id = database.GetNextSequence("PatientRecyclingOrder");
                database.GetCollection("PatientRecyclingOrder").insert_one(make_document(
                        kvp("id", id),
                        kvp("patientId", result.patientId)
                ));
            } else {
                // update patient order in PatientRecyclingOrder
                TagMostRecentPatient(manager, result.patientId);
            }
        }
    }

#endif
}
