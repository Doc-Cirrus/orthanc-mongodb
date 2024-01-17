// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IDatabaseBackendOutput.h"
#include "../Common/DatabasesEnumerations.h"
#include "../Common/DatabaseManager.h"

#include <list>

namespace OrthancDatabases
{
  class IDatabaseBackend : public boost::noncopyable
  {
  public:
    virtual ~IDatabaseBackend()
    {
    }

    virtual OrthancPluginContext* GetContext() = 0;

    virtual IDatabaseFactory* CreateDatabaseFactory() = 0;

    // This function is invoked once, even if multiple connections are open
    virtual void ConfigureDatabase(DatabaseManager& database) = 0;

    virtual void SetOutputFactory(IDatabaseBackendOutput::IFactory* factory) = 0;

    virtual IDatabaseBackendOutput* CreateOutput() = 0;

    virtual bool HasRevisionsSupport() const = 0;

    virtual void AddAttachment(DatabaseManager& manager,
                               int64_t id,
                               const OrthancPluginAttachment& attachment,
                               int64_t revision) = 0;

    virtual void AttachChild(DatabaseManager& manager,
                             int64_t parent,
                             int64_t child) = 0;

    virtual void ClearChanges(DatabaseManager& manager) = 0;

    virtual void ClearExportedResources(DatabaseManager& manager) = 0;

    virtual int64_t CreateResource(DatabaseManager& manager,
                                   const char* publicId,
                                   OrthancPluginResourceType type) = 0;

    virtual void DeleteAttachment(IDatabaseBackendOutput& output,
                                  DatabaseManager& manager,
                                  int64_t id,
                                  int32_t attachment) = 0;

    virtual void DeleteMetadata(DatabaseManager& manager,
                                int64_t id,
                                int32_t metadataType) = 0;

    virtual void DeleteResource(IDatabaseBackendOutput& output,
                                DatabaseManager& manager,
                                int64_t id) = 0;

    virtual void GetAllInternalIds(std::list<int64_t>& target,
                                   DatabaseManager& manager,
                                   OrthancPluginResourceType resourceType) = 0;

    virtual void GetAllPublicIds(std::list<std::string>& target,
                                 DatabaseManager& manager,
                                 OrthancPluginResourceType resourceType) = 0;

    virtual void GetAllPublicIds(std::list<std::string>& target,
                                 DatabaseManager& manager,
                                 OrthancPluginResourceType resourceType,
                                 uint64_t since,
                                 uint64_t limit) = 0;

    /* Use GetOutput().AnswerChange() */
    virtual void GetChanges(IDatabaseBackendOutput& output,
                            bool& done /*out*/,
                            DatabaseManager& manager,
                            int64_t since,
                            uint32_t maxResults) = 0;

    virtual void GetChildrenInternalId(std::list<int64_t>& target /*out*/,
                                       DatabaseManager& manager,
                                       int64_t id) = 0;

    virtual void GetChildrenPublicId(std::list<std::string>& target /*out*/,
                                     DatabaseManager& manager,
                                     int64_t id) = 0;

    /* Use GetOutput().AnswerExportedResource() */
    virtual void GetExportedResources(IDatabaseBackendOutput& output,
                                      bool& done /*out*/,
                                      DatabaseManager& manager,
                                      int64_t since,
                                      uint32_t maxResults) = 0;

    /* Use GetOutput().AnswerChange() */
    virtual void GetLastChange(IDatabaseBackendOutput& output,
                               DatabaseManager& manager) = 0;

    /* Use GetOutput().AnswerExportedResource() */
    virtual void GetLastExportedResource(IDatabaseBackendOutput& output,
                                         DatabaseManager& manager) = 0;

    /* Use GetOutput().AnswerDicomTag() */
    virtual void GetMainDicomTags(IDatabaseBackendOutput& output,
                                  DatabaseManager& manager,
                                  int64_t id) = 0;

    virtual std::string GetPublicId(DatabaseManager& manager,
                                    int64_t resourceId) = 0;

    virtual uint64_t GetResourcesCount(DatabaseManager& manager,
                                       OrthancPluginResourceType resourceType) = 0;

    virtual OrthancPluginResourceType GetResourceType(DatabaseManager& manager,
                                                      int64_t resourceId) = 0;

    virtual uint64_t GetTotalCompressedSize(DatabaseManager& manager) = 0;

    virtual uint64_t GetTotalUncompressedSize(DatabaseManager& manager) = 0;

    virtual bool IsExistingResource(DatabaseManager& manager,
                                    int64_t internalId) = 0;

    virtual bool IsProtectedPatient(DatabaseManager& manager,
                                    int64_t internalId) = 0;

    virtual void ListAvailableMetadata(std::list<int32_t>& target /*out*/,
                                       DatabaseManager& manager,
                                       int64_t id) = 0;

    virtual void ListAvailableAttachments(std::list<int32_t>& target /*out*/,
                                          DatabaseManager& manager,
                                          int64_t id) = 0;

    virtual void LogChange(DatabaseManager& manager,
                           int32_t changeType,
                           int64_t resourceId,
                           OrthancPluginResourceType resourceType,
                           const char* date) = 0;

    virtual void LogExportedResource(DatabaseManager& manager,
                                     const OrthancPluginExportedResource& resource) = 0;

    /* Use GetOutput().AnswerAttachment() */
    virtual bool LookupAttachment(IDatabaseBackendOutput& output,
                                  int64_t& revision /*out*/,
                                  DatabaseManager& manager,
                                  int64_t id,
                                  int32_t contentType) = 0;

    virtual bool LookupGlobalProperty(std::string& target /*out*/,
                                      DatabaseManager& manager,
                                      const char* serverIdentifier,
                                      int32_t property) = 0;

    virtual void LookupIdentifier(std::list<int64_t>& target /*out*/,
                                  DatabaseManager& manager,
                                  OrthancPluginResourceType resourceType,
                                  uint16_t group,
                                  uint16_t element,
                                  OrthancPluginIdentifierConstraint constraint,
                                  const char* value) = 0;

    virtual void LookupIdentifierRange(std::list<int64_t>& target /*out*/,
                                       DatabaseManager& manager,
                                       OrthancPluginResourceType resourceType,
                                       uint16_t group,
                                       uint16_t element,
                                       const char* start,
                                       const char* end) = 0;

    virtual bool LookupMetadata(std::string& target /*out*/,
                                int64_t& revision /*out*/,
                                DatabaseManager& manager,
                                int64_t id,
                                int32_t metadataType) = 0;

    virtual bool LookupParent(int64_t& parentId /*out*/,
                              DatabaseManager& manager,
                              int64_t resourceId) = 0;

    virtual bool LookupResource(int64_t& id /*out*/,
                                OrthancPluginResourceType& type /*out*/,
                                DatabaseManager& manager,
                                const char* publicId) = 0;

    virtual bool SelectPatientToRecycle(int64_t& internalId /*out*/,
                                        DatabaseManager& manager) = 0;

    virtual bool SelectPatientToRecycle(int64_t& internalId /*out*/,
                                        DatabaseManager& manager,
                                        int64_t patientIdToAvoid) = 0;

    virtual void SetGlobalProperty(DatabaseManager& manager,
                                   const char* serverIdentifier,
                                   int32_t property,
                                   const char* utf8) = 0;

    virtual void SetMainDicomTag(DatabaseManager& manager,
                                 int64_t id,
                                 uint16_t group,
                                 uint16_t element,
                                 const char* value) = 0;

    virtual void SetIdentifierTag(DatabaseManager& manager,
                                  int64_t id,
                                  uint16_t group,
                                  uint16_t element,
                                  const char* value) = 0;

    virtual void SetMetadata(DatabaseManager& manager,
                             int64_t id,
                             int32_t metadataType,
                             const char* value,
                             int64_t revision) = 0;

    virtual void SetProtectedPatient(DatabaseManager& manager,
                                     int64_t internalId,
                                     bool isProtected) = 0;

    virtual uint32_t GetDatabaseVersion(DatabaseManager& manager) = 0;

    /**
     * Upgrade the database to the specified version of the database
     * schema.  The upgrade script is allowed to make calls to
     * OrthancPluginReconstructMainDicomTags().
     **/
    virtual void UpgradeDatabase(DatabaseManager& manager,
                                 uint32_t  targetVersion,
                                 OrthancPluginStorageArea* storageArea) = 0;

    virtual void ClearMainDicomTags(DatabaseManager& manager,
                                    int64_t internalId) = 0;

    virtual bool HasCreateInstance() const = 0;

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void LookupResources(IDatabaseBackendOutput& output,
                                 DatabaseManager& manager,
                                 const std::vector<Orthanc::DatabaseConstraint>& lookup,
                                 OrthancPluginResourceType queryLevel,
                                 uint32_t limit,
                                 bool requestSomeInstance) = 0;
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void CreateInstance(OrthancPluginCreateInstanceResult& result,
                                DatabaseManager& manager,
                                const char* hashPatient,
                                const char* hashStudy,
                                const char* hashSeries,
                                const char* hashInstance) = 0;
#endif


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void SetResourcesContent(
      DatabaseManager& manager,
      uint32_t countIdentifierTags,
      const OrthancPluginResourcesContentTags* identifierTags,
      uint32_t countMainDicomTags,
      const OrthancPluginResourcesContentTags* mainDicomTags,
      uint32_t countMetadata,
      const OrthancPluginResourcesContentMetadata* metadata) = 0;
#endif


    virtual void GetChildrenMetadata(std::list<std::string>& target,
                                     DatabaseManager& manager,
                                     int64_t resourceId,
                                     int32_t metadata) = 0;

    virtual int64_t GetLastChangeIndex(DatabaseManager& manager) = 0;

    virtual void TagMostRecentPatient(DatabaseManager& manager,
                                      int64_t patientId) = 0;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
    // NB: "parentPublicId" must be cleared if the resource has no parent
    virtual bool LookupResourceAndParent(int64_t& id,
                                         OrthancPluginResourceType& type,
                                         std::string& parentPublicId,
                                         DatabaseManager& manager,
                                         const char* publicId) = 0;
#  endif
#endif

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
    virtual void GetAllMetadata(std::map<int32_t, std::string>& result,
                                DatabaseManager& manager,
                                int64_t id) = 0;
#  endif
#endif
  };
}
