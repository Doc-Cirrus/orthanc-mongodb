#include "MongoDBBackend.h"

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>


namespace OrthancPlugins
{
	MongoDBBackend::MongoDBBackend(OrthancPluginContext* context, MongoDBConnection* connection,
								   bool useLock, bool allowUnlock) 
		: context_(context)
		//, connection_(connection)
		//, globalProperties_(*connection, useLock, GlobalProperty_IndexLock)
	{
		/*
		globalProperties_.Lock(allowUnlock);

		Prepare();
		*/

		/**
		* Below are the PostgreSQL precompiled statements that are used
		* in more than 1 method of this class.
		**//*

		getPublicId_.reset
		(new PostgreSQLStatement
		(*connection_, "SELECT publicId FROM Resources WHERE internalId=$1"));
		getPublicId_->DeclareInputInteger64(0);

		clearDeletedFiles_.reset
		(new PostgreSQLStatement
		(*connection_, "DELETE FROM DeletedFiles"));

		clearDeletedResources_.reset
		(new PostgreSQLStatement
		(*connection_, "DELETE FROM DeletedResources"));
		*/
	}

	MongoDBBackend::~MongoDBBackend()
	{
		//globalProperties_.Unlock();
	}

	void MongoDBBackend::Open()
	{
		//connection_->Open() {}
	}

	void MongoDBBackend::Close() {}

	void MongoDBBackend::AddAttachment(int64_t id, const OrthancPluginAttachment& attachment) 
	{

		mongocxx::instance inst{};
		mongocxx::client conn{ mongocxx::uri{} };

		bsoncxx::builder::stream::document document{};

		auto collection = conn["orthanc"]["AttachedFiles"];
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

	void MongoDBBackend::AttachChild(int64_t parent, int64_t child) {}

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

	uint32_t MongoDBBackend::GetDatabaseVersion() { return 6; }

	/**
	* Upgrade the database to the specified version of the database
	* schema.  The upgrade script is allowed to make calls to
	* OrthancPluginReconstructMainDicomTags().
	**/
	void MongoDBBackend::UpgradeDatabase(uint32_t  targetVersion, OrthancPluginStorageArea* storageArea) {}

	void MongoDBBackend::ClearMainDicomTags(int64_t internalId) {}


} //namespace OrthancPlugins