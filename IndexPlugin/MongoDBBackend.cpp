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
		auto conn = pool_.acquire();
		auto db = (*conn)[dbname_];
		bsoncxx::builder::stream::document document{};

		auto collection = db["AttachedFiles"];
		document << "id" << id 
				<<  "contentType" << attachment.contentType
				<<	"uuid" << attachment.uuid
				<<  "compressedSize" << static_cast<int64_t>(attachment.compressedSize)
				<<  "uncompressedSize" << static_cast<int64_t>(attachment.uncompressedSize)
				<<  "compressionType" << attachment.compressionType
				<<  "uncompressedHash" << attachment.uncompressedHash
				<<  "compressedHash" << attachment.compressedHash;

		collection.insert_one(document.view());
	}

	void MongoDBBackend::AttachChild(int64_t parent, int64_t child) {
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

	void MongoDBBackend::ClearChanges() {}

	void MongoDBBackend::ClearExportedResources() {}

	int64_t MongoDBBackend::CreateResource(const char* publicId, OrthancPluginResourceType type) { return 1; }

	void MongoDBBackend::DeleteAttachment(int64_t id, int32_t attachment) {}

	void MongoDBBackend::DeleteMetadata(int64_t id, int32_t metadataType) {}

	void MongoDBBackend::DeleteResource(int64_t id) {}

	void MongoDBBackend::GetAllInternalIds(std::list<int64_t>& target, OrthancPluginResourceType resourceType) {}

	void MongoDBBackend::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType) {}

	void MongoDBBackend::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType,
										 uint64_t since, uint64_t limit) {}

	/* Use GetOutput().AnswerChange() */
	void MongoDBBackend::GetChanges(bool& done /*out*/, int64_t since, uint32_t maxResults) {}

	void MongoDBBackend::GetChildrenInternalId(std::list<int64_t>& target /*out*/, int64_t id) {}

	void MongoDBBackend::GetChildrenPublicId(std::list<std::string>& target /*out*/, int64_t id) {}

	/* Use GetOutput().AnswerExportedResource() */
	void MongoDBBackend::GetExportedResources(bool& done /*out*/, int64_t since, uint32_t maxResults) {}

	/* Use GetOutput().AnswerChange() */
	void MongoDBBackend::GetLastChange() {}

	/* Use GetOutput().AnswerExportedResource() */
	void MongoDBBackend::GetLastExportedResource() {}

	/* Use GetOutput().AnswerDicomTag() */
	void MongoDBBackend::GetMainDicomTags(int64_t id) {}

	std::string MongoDBBackend::GetPublicId(int64_t resourceId) { return ""; }

	uint64_t MongoDBBackend::GetResourceCount(OrthancPluginResourceType resourceType) { return 1; }

	OrthancPluginResourceType MongoDBBackend::GetResourceType(int64_t resourceId) 
	{
		return static_cast<OrthancPluginResourceType>(0);
	}

	uint64_t MongoDBBackend::GetTotalCompressedSize() { return 1; }

	uint64_t MongoDBBackend::GetTotalUncompressedSize() { return 1; }

	bool MongoDBBackend::IsExistingResource(int64_t internalId) { return false; }

	bool MongoDBBackend::IsProtectedPatient(int64_t internalId) { return false; }

	void MongoDBBackend::ListAvailableMetadata(std::list<int32_t>& target /*out*/, int64_t id) {}

	void MongoDBBackend::ListAvailableAttachments(std::list<int32_t>& target /*out*/, int64_t id) {}

	void MongoDBBackend::LogChange(const OrthancPluginChange& change) {}

	void MongoDBBackend::LogExportedResource(const OrthancPluginExportedResource& resource) {}

	/* Use GetOutput().AnswerAttachment() */
	bool MongoDBBackend::LookupAttachment(int64_t id, int32_t contentType) { return false; }

	bool MongoDBBackend::LookupGlobalProperty(std::string& target /*out*/, int32_t property) { return false; }

	void MongoDBBackend::LookupIdentifier(std::list<int64_t>& target /*out*/,
		OrthancPluginResourceType resourceType,
		uint16_t group,
		uint16_t element,
		OrthancPluginIdentifierConstraint constraint,
		const char* value) {}

	bool MongoDBBackend::LookupMetadata(std::string& target /*out*/, int64_t id, int32_t metadataType) { return false; }

	bool MongoDBBackend::LookupParent(int64_t& parentId /*out*/, int64_t resourceId) { return false; }

	bool MongoDBBackend::LookupResource(int64_t& id /*out*/, OrthancPluginResourceType& type /*out*/, const char* publicId) { return false; }

	bool MongoDBBackend::SelectPatientToRecycle(int64_t& internalId /*out*/) { return false; }

	bool MongoDBBackend::SelectPatientToRecycle(int64_t& internalId /*out*/, int64_t patientIdToAvoid) { return false; }

	void MongoDBBackend::SetGlobalProperty(int32_t property, const char* value) {}

	void MongoDBBackend::SetMainDicomTag(int64_t id, uint16_t group, uint16_t element, const char* value) {}

	void MongoDBBackend::SetIdentifierTag(int64_t id, uint16_t group, uint16_t element, const char* value) {}

	void MongoDBBackend::SetMetadata(int64_t id, int32_t metadataType, const char* value) {}

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

	void MongoDBBackend::ClearMainDicomTags(int64_t internalId) {}


} //namespace OrthancPlugins