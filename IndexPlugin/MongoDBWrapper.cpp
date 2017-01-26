#include "MongoDBWrapper.h"

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>


namespace OrthancPlugins
{
	MongoDBWrapper::MongoDBWrapper(OrthancPluginContext* context, MongoDBConnection* connection,
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

	MongoDBWrapper::~MongoDBWrapper()
	{
		//globalProperties_.Unlock();
	}

	void MongoDBWrapper::Open()
	{
		//connection_->Open() {}
	}

	void MongoDBWrapper::Close() {}

	void MongoDBWrapper::AddAttachment(int64_t id, const OrthancPluginAttachment& attachment) 
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

	void MongoDBWrapper::AttachChild(int64_t parent, int64_t child) {}

	void MongoDBWrapper::ClearChanges() {}

	void MongoDBWrapper::ClearExportedResources() {}

	int64_t MongoDBWrapper::CreateResource(const char* publicId, OrthancPluginResourceType type) { return 1; }

	void MongoDBWrapper::DeleteAttachment(int64_t id, int32_t attachment) {}

	void MongoDBWrapper::DeleteMetadata(int64_t id, int32_t metadataType) {}

	void MongoDBWrapper::DeleteResource(int64_t id) {}

	void MongoDBWrapper::GetAllInternalIds(std::list<int64_t>& target, OrthancPluginResourceType resourceType) {}

	void MongoDBWrapper::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType) {}

	void MongoDBWrapper::GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType,
										 uint64_t since, uint64_t limit) {}

	/* Use GetOutput().AnswerChange() */
	void MongoDBWrapper::GetChanges(bool& done /*out*/, int64_t since, uint32_t maxResults) {}

	void MongoDBWrapper::GetChildrenInternalId(std::list<int64_t>& target /*out*/, int64_t id) {}

	void MongoDBWrapper::GetChildrenPublicId(std::list<std::string>& target /*out*/, int64_t id) {}

	/* Use GetOutput().AnswerExportedResource() */
	void MongoDBWrapper::GetExportedResources(bool& done /*out*/, int64_t since, uint32_t maxResults) {}

	/* Use GetOutput().AnswerChange() */
	void MongoDBWrapper::GetLastChange() {}

	/* Use GetOutput().AnswerExportedResource() */
	void MongoDBWrapper::GetLastExportedResource() {}

	/* Use GetOutput().AnswerDicomTag() */
	void MongoDBWrapper::GetMainDicomTags(int64_t id) {}

	std::string MongoDBWrapper::GetPublicId(int64_t resourceId) { return ""; }

	uint64_t MongoDBWrapper::GetResourceCount(OrthancPluginResourceType resourceType) { return 1; }

	OrthancPluginResourceType MongoDBWrapper::GetResourceType(int64_t resourceId) 
	{
		return static_cast<OrthancPluginResourceType>(0);
	}

	uint64_t MongoDBWrapper::GetTotalCompressedSize() { return 1; }

	uint64_t MongoDBWrapper::GetTotalUncompressedSize() { return 1; }

	bool MongoDBWrapper::IsExistingResource(int64_t internalId) { return false; }

	bool MongoDBWrapper::IsProtectedPatient(int64_t internalId) { return false; }

	void MongoDBWrapper::ListAvailableMetadata(std::list<int32_t>& target /*out*/, int64_t id) {}

	void MongoDBWrapper::ListAvailableAttachments(std::list<int32_t>& target /*out*/, int64_t id) {}

	void MongoDBWrapper::LogChange(const OrthancPluginChange& change) {}

	void MongoDBWrapper::LogExportedResource(const OrthancPluginExportedResource& resource) {}

	/* Use GetOutput().AnswerAttachment() */
	bool MongoDBWrapper::LookupAttachment(int64_t id, int32_t contentType) { return false; }

	bool MongoDBWrapper::LookupGlobalProperty(std::string& target /*out*/, int32_t property) { return false; }

	void MongoDBWrapper::LookupIdentifier(std::list<int64_t>& target /*out*/,
		OrthancPluginResourceType resourceType,
		uint16_t group,
		uint16_t element,
		OrthancPluginIdentifierConstraint constraint,
		const char* value) {}

	bool MongoDBWrapper::LookupMetadata(std::string& target /*out*/, int64_t id, int32_t metadataType) { return false; }

	bool MongoDBWrapper::LookupParent(int64_t& parentId /*out*/, int64_t resourceId) { return false; }

	bool MongoDBWrapper::LookupResource(int64_t& id /*out*/, OrthancPluginResourceType& type /*out*/, const char* publicId) { return false; }

	bool MongoDBWrapper::SelectPatientToRecycle(int64_t& internalId /*out*/) { return false; }

	bool MongoDBWrapper::SelectPatientToRecycle(int64_t& internalId /*out*/, int64_t patientIdToAvoid) { return false; }

	void MongoDBWrapper::SetGlobalProperty(int32_t property, const char* value) {}

	void MongoDBWrapper::SetMainDicomTag(int64_t id, uint16_t group, uint16_t element, const char* value) {}

	void MongoDBWrapper::SetIdentifierTag(int64_t id, uint16_t group, uint16_t element, const char* value) {}

	void MongoDBWrapper::SetMetadata(int64_t id, int32_t metadataType, const char* value) {}

	void MongoDBWrapper::SetProtectedPatient(int64_t internalId, bool isProtected) {}

	void MongoDBWrapper::StartTransaction() {}

	void MongoDBWrapper::RollbackTransaction() {}

	void MongoDBWrapper::CommitTransaction() {}

	uint32_t MongoDBWrapper::GetDatabaseVersion() { return 6; }

	/**
	* Upgrade the database to the specified version of the database
	* schema.  The upgrade script is allowed to make calls to
	* OrthancPluginReconstructMainDicomTags().
	**/
	void MongoDBWrapper::UpgradeDatabase(uint32_t  targetVersion, OrthancPluginStorageArea* storageArea) {}

	void MongoDBWrapper::ClearMainDicomTags(int64_t internalId) {}


} //namespace OrthancPlugins