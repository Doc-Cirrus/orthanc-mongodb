/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 DocCirrus, Germany
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General 
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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

#include <boost/current_function.hpp>


namespace OrthancPlugins
{
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
	}

	MongoDBBackend::~MongoDBBackend()
	{
	}

	void MongoDBBackend::Open()	{}

	void MongoDBBackend::Close() {}

	void MongoDBBackend::AddAttachment(int64_t id, const OrthancPluginAttachment& attachment) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		bsoncxx::builder::stream::document document{};

		auto collection = db["AttachedFiles"];
		document << "id" << id 
				<<  "fileType" << attachment.contentType
				<<	"uuid" << attachment.uuid
				<<  "compressedSize" << static_cast<int64_t>(attachment.compressedSize)
				<<  "uncompressedSize" << static_cast<int64_t>(attachment.uncompressedSize)
				<<  "compressionType" << attachment.compressionType
				<<  "uncompressedHash" << attachment.uncompressedHash
				<<  "compressedHash" << attachment.compressedHash;

		collection.insert_one(document.view());
	}

	void MongoDBBackend::AttachChild(int64_t parent, int64_t child) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		auto collection = db["Resources"];

		using namespace bsoncxx::builder::stream;
		collection.update_many(
			document{} << "internalId" << static_cast<int64_t>(child) << finalize,
			document{} << "$set" << open_document <<
                        "parentId" << static_cast<int64_t>(parent) << close_document << finalize
		);
	}

	void MongoDBBackend::ClearChanges() 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		auto collection = db["Changes"];
		collection.delete_many(bsoncxx::builder::stream::document{} << bsoncxx::builder::stream::finalize);
	}

	void MongoDBBackend::ClearExportedResources() {
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		auto collection = db["ExportedResources"];
		collection.delete_many(bsoncxx::builder::stream::document{} << bsoncxx::builder::stream::finalize);
	}

	int64_t MongoDBBackend::GetNextSequence(mongocxx::database& db, const std::string seqName) {
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;
		
		boost::mutex::scoped_lock lock(mutex_);

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
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		bsoncxx::builder::stream::document document{};

		int64_t seq = GetNextSequence(db, "Resources");

		auto collection = db["Resources"];
		document << "internalId" << seq 
				<<  "resourceType" << static_cast<int>(type)
				<<	"publicId" << publicId
				<<  "parentId" << bsoncxx::types::b_null();

		collection.insert_one(document.view());
		return seq; 
	}

	void MongoDBBackend::DeleteAttachment(int64_t id, int32_t attachment) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		auto collection = db["AttachedFiles"];
		collection.delete_many(
			document{} << "id" << static_cast<int64_t>(id)
					<< "fileType" << attachment	<< finalize);
	}

	void MongoDBBackend::DeleteMetadata(int64_t id, int32_t metadataType) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		auto collection = db["Metadata"];
		collection.delete_many(
			document{} << "id" << static_cast<int64_t>(id)
					<< "type" << metadataType << finalize);
	}

	void MongoDBBackend::DeleteResource(int64_t id) {
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		db["Metadata"].delete_many(document{} << "id" << id << finalize);
		db["AttachedFiles"].delete_many(document{} << "id" << id << finalize);
		db["Changes"].delete_many(document{} << "internalId" << id << finalize);
		db["PatientRecyclingOrder"].delete_many(document{} << "patientId" << id << finalize);
		db["MainDicomTags"].delete_many(document{} << "id" << id << finalize);
		db["DicomIdentifiers"].delete_many(document{} << "id" << id << finalize);
		
		//TODO: delete all parent resources as well
		db["Resources"].delete_many(document{} << "internalId" << id << finalize);

		//TODO:
		/*
		 PostgreSQLResult result(*getRemainingAncestor_);
		if (!result.IsDone())
		{
		GetOutput().SignalRemainingAncestor(result.GetString(1),
											static_cast<OrthancPluginResourceType>(result.GetInteger(0)));

		// There is at most 1 remaining ancestor
		assert((result.Step(), result.IsDone()));
		}

		SignalDeletedFilesAndResources();
		*/
	}

	void MongoDBBackend::GetAllInternalIds(std::list<int64_t>& target, OrthancPluginResourceType resourceType) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto cursor = db["Resources"].find(
			document{} << "resourceType" << static_cast<int>(resourceType) << finalize);
		for(auto doc : cursor) {
  			target.push_back(doc["internalId"].get_int64().value);
		}
	}

	void MongoDBBackend::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto cursor = db["Resources"].find(
			document{} << "resourceType" << static_cast<int>(resourceType) << finalize);
		for(auto doc : cursor) {
  			target.push_back(doc["publicId"].get_utf8().value.to_string());
		}
	}

	void MongoDBBackend::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType,
										 uint64_t since, uint64_t limit) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		mongocxx::options::find options{};
		options.limit(limit).skip(since);

		auto cursor = db["Resources"].find(
			document{} << "resourceType" << static_cast<int>(resourceType) << finalize, options);
		for(auto doc : cursor) {
  			target.push_back(doc["publicId"].get_utf8().value.to_string());
		}
	}

	/* Use GetOutput().AnswerChange() */
	void MongoDBBackend::GetChanges(bool& done /*out*/, int64_t since, uint32_t maxResults) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		mongocxx::options::find options{};
		options.sort(document{} << "seq" << 1 << finalize).limit(maxResults + 1);

		auto cursor = db["Changes"].find(
			document{} << "seq" << open_document << "$gt" << since << close_document << finalize, options);
		uint32_t count = 0;
		done = true;
		for(auto doc : cursor) {
			if (count == maxResults) 
			{
				done = false;
				break;
			}
			GetOutput().AnswerChange(
				doc["seq"].get_int64().value,
				doc["changeType"].get_int32().value,
				static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
				GetPublicId(doc["internalId"].get_int64().value),
				doc["date"].get_utf8().value.to_string());
			count++;
		}
	}

	void MongoDBBackend::GetChildrenInternalId(std::list<int64_t>& target /*out*/, int64_t id) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto cursor = db["Resources"].find(document{} << "parentId" << id << finalize);
		for(auto doc : cursor) {
  			target.push_back(doc["internalId"].get_int64().value);
		}
	}

	void MongoDBBackend::GetChildrenPublicId(std::list<std::string>& target /*out*/, int64_t id) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto cursor = db["Resources"].find(
			document{} << "parentId" << id << finalize);
		for(auto doc : cursor) {
  			target.push_back(doc["publicId"].get_utf8().value.to_string());
		}
	}

	/* Use GetOutput().AnswerExportedResource() */
	void MongoDBBackend::GetExportedResources(bool& done /*out*/, int64_t since, uint32_t maxResults) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		mongocxx::options::find options{};
		options.sort(document{} << "seq" << 1 << finalize).limit(maxResults + 1);

		auto cursor = db["ExportedResources"].find(
			document{} << "seq" << open_document << "$gt" << since << close_document << finalize, options);
		int count = 0;
		done = true;
		for(auto doc : cursor) {
			if (count == maxResults) {
				done = false;
				break;
			}
			GetOutput().AnswerExportedResource(
				doc["seq"].get_int64().value,
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
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		mongocxx::options::find options{};
		options.sort(document{} << "seq" << -1 << finalize).limit(1);

		auto cursor = db["Changes"].find(
			document{} << finalize, options);
		for(auto doc : cursor) {
			GetOutput().AnswerChange(
				doc["seq"].get_int64().value,
				doc["changeType"].get_int32().value,
				static_cast<OrthancPluginResourceType>(doc["resourceType"].get_int32().value),
				GetPublicId(doc["internalId"].get_int64().value),
				doc["date"].get_utf8().value.to_string());
		}
	}

	/* Use GetOutput().AnswerExportedResource() */
	void MongoDBBackend::GetLastExportedResource() 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		mongocxx::options::find options{};
		options.sort(document{} << "seq" << 11 << finalize).limit(1);

		auto cursor = db["ExportedResources"].find(document{} << finalize, options);
		for(auto doc : cursor) {
			GetOutput().AnswerExportedResource(
				doc["seq"].get_int64().value,
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
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto cursor = db["MainDicomTags"].find(
			document{} << "id" << id << finalize);
		for(auto doc : cursor) {
			GetOutput().AnswerDicomTag(static_cast<uint16_t>(doc["tagGroup"].get_int32().value),
                                   static_cast<uint16_t>(doc["tagElement"].get_int32().value),
                                   doc["value"].get_utf8().value.to_string());
		}
	}

	std::string MongoDBBackend::GetPublicId(int64_t resourceId) 
	{ 
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
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

	uint64_t MongoDBBackend::GetResourceCount(OrthancPluginResourceType resourceType) { 
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		int64_t count = db["Resources"].count(
			document{} << "resourceType" << static_cast<int>(resourceType) << finalize);
		return count;
	}

	OrthancPluginResourceType MongoDBBackend::GetResourceType(int64_t resourceId) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto result = db["Resources"].find_one(document{} << "id" << resourceId << finalize);
    
		if (result)
		{ 
			return static_cast<OrthancPluginResourceType>(result->view()["resourceType"].get_int32().value);
		}
		throw MongoDBException("Unknown resource");
	}

	uint64_t MongoDBBackend::GetTotalCompressedSize() 
	{ 
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		mongocxx::pipeline stages;
        bsoncxx::builder::stream::document group_stage;

        group_stage << "_id" << bsoncxx::types::b_null()
                    << "totalSize" << open_document << "$sum" << "$compressedSize" << close_document;

        stages.group(group_stage.view());

        auto cursor = db["AttachedFiles"].aggregate(stages);

        for (auto&& doc : cursor) {
            return doc["totalSize"].get_int64().value;
        }
		
		return 0; 
	}

	uint64_t MongoDBBackend::GetTotalUncompressedSize() 
	{ 
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		mongocxx::pipeline stages;
        bsoncxx::builder::stream::document group_stage;

        group_stage << "_id" << bsoncxx::types::b_null()
                    << "totalSize" << open_document << "$sum" << "$uncompressedSize" << close_document;

        stages.group(group_stage.view());

        auto cursor = db["AttachedFiles"].aggregate(stages);

        for (auto&& doc : cursor) {
            return doc["totalSize"].get_int64().value;
        }
		
		return 0; 	
	}

	bool MongoDBBackend::IsExistingResource(int64_t internalId) 
	{ 
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		int64_t count = db["Resources"].count(
			document{} << "internalId" << internalId << finalize);
		return count > 0;
	}

	bool MongoDBBackend::IsProtectedPatient(int64_t internalId) 
	{ 
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		int64_t count = db["PatientRecyclingOrder"].count(
			document{} << "patientId" << internalId << finalize);
		return count > 0;
	}

	void MongoDBBackend::ListAvailableMetadata(std::list<int32_t>& target /*out*/, int64_t id) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto cursor = db["Metadata"].find(
			document{} << "id" << id << finalize);
		for(auto doc : cursor) {
  			target.push_back(doc["type"].get_int32().value);
		}
	}

	void MongoDBBackend::ListAvailableAttachments(std::list<int32_t>& target /*out*/, int64_t id) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto cursor = db["AttachedFiles"].find(
			document{} << "id" << id << finalize);
		for(auto doc : cursor) {
  			target.push_back(doc["fileType"].get_int32().value);
		}
	}

	void MongoDBBackend::LogChange(const OrthancPluginChange& change) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		bsoncxx::builder::stream::document document{};

		int64_t seq = GetNextSequence(db, "Changes");

		int64_t id;
		OrthancPluginResourceType type;
		if (!LookupResource(id, type, change.publicId) ||
			type != change.resourceType)
		{
			throw MongoDBException();
		}

		auto collection = db["Changes"];
		document << "id" << seq 
				<<  "changeType" << change.changeType
				<<	"internalId" << id
				<<  "resourceType" << change.resourceType
				<<  "date" << change.date;

		collection.insert_one(document.view());
	}

	void MongoDBBackend::LogExportedResource(const OrthancPluginExportedResource& resource) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		bsoncxx::builder::stream::document document{};

		int64_t seq = GetNextSequence(db, "ExportedResources");

		auto collection = db["ExportedResources"];
		document << "id" << seq 
				<<  "resourceType" << resource.resourceType
				<<	"publicId" << resource.publicId
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
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto doc = db["AttachedFiles"].find_one(
			document{} << "id" << id << "fileType" << contentType << finalize);
		if(doc) {
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
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto doc = db["GlobalProperties"].find_one(
			document{} << "property" << property << finalize);
		if(doc) {
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
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto cursor = db["DicomIdentifiers"].find(
			document{} << "tagGroup" << group 
					<< "tagElement" << element
					<< "value" << value	<< finalize);
		for(auto doc : cursor) {
  			target.push_back(doc["id"].get_int64().value);
		} 
	}

	bool MongoDBBackend::LookupMetadata(std::string& target /*out*/, int64_t id, int32_t metadataType) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto doc = db["Metadata"].find_one(
			document{} << "id" << id << "type" << metadataType << finalize);
		if(doc) {
			bsoncxx::document::view view = doc->view();
			target = view["value"].get_utf8().value.to_string();
			return true;
		}
		return false; 
	}

	bool MongoDBBackend::LookupParent(int64_t& parentId /*out*/, int64_t resourceId) 
	{ 
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		auto doc = db["Resources"].find_one(
			document{} << "internalId" << resourceId << finalize);
		bool res = false;
		if (doc) {
			bsoncxx::document::element parent = doc->view()["parentId"];
			if (parent.type() == bsoncxx::type::k_int64) {
				parentId = parent.get_int64().value;
				res = true;
			}
		}
		return res;
	}

	bool MongoDBBackend::LookupResource(int64_t& id /*out*/, OrthancPluginResourceType& type /*out*/, const char* publicId) 
	{ 
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		auto doc = db["Resources"].find_one(
			document{} << "publicId" << publicId << finalize);
		if(doc) {
			bsoncxx::document::view view = doc->view();
			id = view["internalId"].get_int64().value;
			type = static_cast<OrthancPluginResourceType>(view["resourceType"].get_int32().value);
			return true;
		}
		return false; 
	}

	bool MongoDBBackend::SelectPatientToRecycle(int64_t& internalId /*out*/) { return false; }

	bool MongoDBBackend::SelectPatientToRecycle(int64_t& internalId /*out*/, int64_t patientIdToAvoid) { return false; }

	void MongoDBBackend::SetGlobalProperty(int32_t property, const char* value) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
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
		if (!doc) {
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
				<<	"tagElement" << element
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
				<<	"tagElement" << element
				<<  "value" << value;

		collection.insert_one(document.view());
	}

	void MongoDBBackend::SetMetadata(int64_t id, int32_t metadataType, const char* value) 
	{
		//todo: delete
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		bsoncxx::builder::stream::document document{};

		auto collection = db["Metadata"];
		document << "id" << id 
				<<  "type" << metadataType
				<<  "value" << value;

		collection.insert_one(document.view());
	}

	void MongoDBBackend::SetProtectedPatient(int64_t internalId, bool isProtected) {}

	void MongoDBBackend::StartTransaction() {}

	void MongoDBBackend::RollbackTransaction() {}

	void MongoDBBackend::CommitTransaction() {}

	uint32_t MongoDBBackend::GetDatabaseVersion() { return GlobalProperty_DatabaseSchemaVersion; }

	/**
	* Upgrade the database to the specified version of the database
	* schema.  The upgrade script is allowed to make calls to
	* OrthancPluginReconstructMainDicomTags().
	**/
	void MongoDBBackend::UpgradeDatabase(uint32_t  targetVersion, OrthancPluginStorageArea* storageArea) {}

	void MongoDBBackend::ClearMainDicomTags(int64_t internalId) 
	{
		OrthancPluginLogInfo(context_, (std::string("Entering: ") + BOOST_CURRENT_FUNCTION).c_str());
		using namespace bsoncxx::builder::stream;

		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];

		db["MainDicomTags"].delete_many(document{} << "id" << internalId << finalize);
		db["DicomIdentifiers"].delete_many(document{} << "id" << internalId << finalize);
		
	}

} //namespace OrthancPlugins
