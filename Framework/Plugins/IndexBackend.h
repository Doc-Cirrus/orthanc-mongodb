// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IDatabaseBackend.h"

#include <OrthancException.h>

#include <boost/thread/shared_mutex.hpp>


namespace OrthancDatabases
{
  /**
   * WARNING: This class can be invoked concurrently by several
   * threads if it is used from "DatabaseBackendAdapterV3".
   **/
  class IndexBackend : public IDatabaseBackend
  {
  private:
    class LookupFormatter;

    OrthancPluginContext*  context_;

    boost::shared_mutex                                outputFactoryMutex_;
    std::unique_ptr<IDatabaseBackendOutput::IFactory>  outputFactory_;

  protected:
    void ClearDeletedFiles(DatabaseManager& manager);

    void ClearDeletedResources(DatabaseManager& manager);

    void SignalDeletedFiles(IDatabaseBackendOutput& output,
                            DatabaseManager& manager);

    void SignalDeletedResources(IDatabaseBackendOutput& output,
                                DatabaseManager& manager);

  private:
    void ReadChangesInternal(IDatabaseBackendOutput& output,
                             bool& done,
                             DatabaseManager& manager,
                             DatabaseManager::CachedStatement& statement,
                             const Dictionary& args,
                             uint32_t maxResults);

    void ReadExportedResourcesInternal(IDatabaseBackendOutput& output,
                                       bool& done,
                                       DatabaseManager::CachedStatement& statement,
                                       const Dictionary& args,
                                       uint32_t maxResults);

  public:
    explicit IndexBackend(OrthancPluginContext* context);

    virtual OrthancPluginContext* GetContext() ORTHANC_OVERRIDE
    {
      return context_;
    }

    virtual void SetOutputFactory(IDatabaseBackendOutput::IFactory* factory) ORTHANC_OVERRIDE;

    virtual IDatabaseBackendOutput* CreateOutput() ORTHANC_OVERRIDE;

    virtual void AddAttachment(DatabaseManager& manager,
                               int64_t id,
                               const OrthancPluginAttachment& attachment,
                               int64_t revision) ORTHANC_OVERRIDE;

    virtual void AttachChild(DatabaseManager& manager,
                             int64_t parent,
                             int64_t child) ORTHANC_OVERRIDE;

    virtual void ClearChanges(DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual void ClearExportedResources(DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual void DeleteAttachment(IDatabaseBackendOutput& output,
                                  DatabaseManager& manager,
                                  int64_t id,
                                  int32_t attachment) ORTHANC_OVERRIDE;

    virtual void DeleteMetadata(DatabaseManager& manager,
                                int64_t id,
                                int32_t metadataType) ORTHANC_OVERRIDE;

    virtual void DeleteResource(IDatabaseBackendOutput& output,
                                DatabaseManager& manager,
                                int64_t id) ORTHANC_OVERRIDE;

    virtual void GetAllInternalIds(std::list<int64_t>& target,
                                   DatabaseManager& manager,
                                   OrthancPluginResourceType resourceType) ORTHANC_OVERRIDE;

    virtual void GetAllPublicIds(std::list<std::string>& target,
                                 DatabaseManager& manager,
                                 OrthancPluginResourceType resourceType) ORTHANC_OVERRIDE;

    virtual void GetAllPublicIds(std::list<std::string>& target,
                                 DatabaseManager& manager,
                                 OrthancPluginResourceType resourceType,
                                 uint64_t since,
                                 uint64_t limit) ORTHANC_OVERRIDE;

    virtual void GetChanges(IDatabaseBackendOutput& output,
                            bool& done /*out*/,
                            DatabaseManager& manager,
                            int64_t since,
                            uint32_t maxResults) ORTHANC_OVERRIDE;

    virtual void GetChildrenInternalId(std::list<int64_t>& target /*out*/,
                                       DatabaseManager& manager,
                                       int64_t id) ORTHANC_OVERRIDE;

    virtual void GetChildrenPublicId(std::list<std::string>& target /*out*/,
                                     DatabaseManager& manager,
                                     int64_t id) ORTHANC_OVERRIDE;

    virtual void GetExportedResources(IDatabaseBackendOutput& output,
                                      bool& done /*out*/,
                                      DatabaseManager& manager,
                                      int64_t since,
                                      uint32_t maxResults) ORTHANC_OVERRIDE;

    virtual void GetLastChange(IDatabaseBackendOutput& output,
                               DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual void GetLastExportedResource(IDatabaseBackendOutput& output,
                                         DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual void GetMainDicomTags(IDatabaseBackendOutput& output,
                                  DatabaseManager& manager,
                                  int64_t id) ORTHANC_OVERRIDE;

    virtual std::string GetPublicId(DatabaseManager& manager,
                                    int64_t resourceId) ORTHANC_OVERRIDE;

    virtual uint64_t GetResourcesCount(DatabaseManager& manager,
                                       OrthancPluginResourceType resourceType) ORTHANC_OVERRIDE;

    virtual OrthancPluginResourceType GetResourceType(DatabaseManager& manager,
                                                      int64_t resourceId) ORTHANC_OVERRIDE;

    virtual uint64_t GetTotalCompressedSize(DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual uint64_t GetTotalUncompressedSize(DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual bool IsExistingResource(DatabaseManager& manager,
                                    int64_t internalId) ORTHANC_OVERRIDE;

    virtual bool IsProtectedPatient(DatabaseManager& manager,
                                    int64_t internalId) ORTHANC_OVERRIDE;

    virtual void ListAvailableMetadata(std::list<int32_t>& target /*out*/,
                                       DatabaseManager& manager,
                                       int64_t id) ORTHANC_OVERRIDE;

    virtual void ListAvailableAttachments(std::list<int32_t>& target /*out*/,
                                          DatabaseManager& manager,
                                          int64_t id) ORTHANC_OVERRIDE;

    virtual void LogChange(DatabaseManager& manager,
                           int32_t changeType,
                           int64_t resourceId,
                           OrthancPluginResourceType resourceType,
                           const char* date) ORTHANC_OVERRIDE;

    virtual void LogExportedResource(DatabaseManager& manager,
                                     const OrthancPluginExportedResource& resource) ORTHANC_OVERRIDE;

    virtual bool LookupAttachment(IDatabaseBackendOutput& output,
                                  int64_t& revision /*out*/,
                                  DatabaseManager& manager,
                                  int64_t id,
                                  int32_t contentType) ORTHANC_OVERRIDE;

    virtual bool LookupGlobalProperty(std::string& target /*out*/,
                                      DatabaseManager& manager,
                                      const char* serverIdentifier,
                                      int32_t property) ORTHANC_OVERRIDE;

    virtual void LookupIdentifier(std::list<int64_t>& target /*out*/,
                                  DatabaseManager& manager,
                                  OrthancPluginResourceType resourceType,
                                  uint16_t group,
                                  uint16_t element,
                                  OrthancPluginIdentifierConstraint constraint,
                                  const char* value) ORTHANC_OVERRIDE;

    virtual void LookupIdentifierRange(std::list<int64_t>& target /*out*/,
                                       DatabaseManager& manager,
                                       OrthancPluginResourceType resourceType,
                                       uint16_t group,
                                       uint16_t element,
                                       const char* start,
                                       const char* end) ORTHANC_OVERRIDE;

    virtual bool LookupMetadata(std::string& target /*out*/,
                                int64_t& revision /*out*/,
                                DatabaseManager& manager,
                                int64_t id,
                                int32_t metadataType) ORTHANC_OVERRIDE;

    virtual bool LookupParent(int64_t& parentId /*out*/,
                              DatabaseManager& manager,
                              int64_t resourceId) ORTHANC_OVERRIDE;

    virtual bool LookupResource(int64_t& id /*out*/,
                                OrthancPluginResourceType& type /*out*/,
                                DatabaseManager& manager,
                                const char* publicId) ORTHANC_OVERRIDE;

    virtual bool SelectPatientToRecycle(int64_t& internalId /*out*/,
                                        DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual bool SelectPatientToRecycle(int64_t& internalId /*out*/,
                                        DatabaseManager& manager,
                                        int64_t patientIdToAvoid) ORTHANC_OVERRIDE;

    virtual void SetGlobalProperty(DatabaseManager& manager,
                                   const char* serverIdentifier,
                                   int32_t property,
                                   const char* utf8) ORTHANC_OVERRIDE;

    virtual void SetMainDicomTag(DatabaseManager& manager,
                                 int64_t id,
                                 uint16_t group,
                                 uint16_t element,
                                 const char* value) ORTHANC_OVERRIDE;

    virtual void SetIdentifierTag(DatabaseManager& manager,
                                  int64_t id,
                                  uint16_t group,
                                  uint16_t element,
                                  const char* value) ORTHANC_OVERRIDE;

    virtual void SetMetadata(DatabaseManager& manager,
                             int64_t id,
                             int32_t metadataType,
                             const char* value,
                             int64_t revision) ORTHANC_OVERRIDE;

    virtual void SetProtectedPatient(DatabaseManager& manager,
                                     int64_t internalId,
                                     bool isProtected) ORTHANC_OVERRIDE;

    virtual uint32_t GetDatabaseVersion(DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual void UpgradeDatabase(DatabaseManager& manager,
                                 uint32_t  targetVersion,
                                 OrthancPluginStorageArea* storageArea) ORTHANC_OVERRIDE;

    virtual void ClearMainDicomTags(DatabaseManager& manager,
                                    int64_t internalId) ORTHANC_OVERRIDE;

    // For unit testing only!
    virtual uint64_t GetAllResourcesCount(DatabaseManager& manager);

    // For unit testing only!
    virtual uint64_t GetUnprotectedPatientsCount(DatabaseManager& manager);

    // For unit testing only!
    virtual bool GetParentPublicId(std::string& target,
                                   DatabaseManager& manager,
                                   int64_t id);

    // For unit tests only!
    virtual void GetChildren(std::list<std::string>& childrenPublicIds,
                             DatabaseManager& manager,
                             int64_t id);

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    // New primitive since Orthanc 1.5.2
    virtual void LookupResources(IDatabaseBackendOutput& output,
                                 DatabaseManager& manager,
                                 const std::vector<Orthanc::DatabaseConstraint>& lookup,
                                 OrthancPluginResourceType queryLevel,
                                 uint32_t limit,
                                 bool requestSomeInstance) ORTHANC_OVERRIDE;
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    // New primitive since Orthanc 1.5.2
    virtual void SetResourcesContent(
      DatabaseManager& manager,
      uint32_t countIdentifierTags,
      const OrthancPluginResourcesContentTags* identifierTags,
      uint32_t countMainDicomTags,
      const OrthancPluginResourcesContentTags* mainDicomTags,
      uint32_t countMetadata,
      const OrthancPluginResourcesContentMetadata* metadata) ORTHANC_OVERRIDE;
#endif

    // New primitive since Orthanc 1.5.2
    virtual void GetChildrenMetadata(std::list<std::string>& target,
                                     DatabaseManager& manager,
                                     int64_t resourceId,
                                     int32_t metadata) ORTHANC_OVERRIDE;

    virtual void TagMostRecentPatient(DatabaseManager& manager,
                                      int64_t patient) ORTHANC_OVERRIDE;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
    // New primitive since Orthanc 1.5.4
    virtual bool LookupResourceAndParent(int64_t& id,
                                         OrthancPluginResourceType& type,
                                         std::string& parentPublicId,
                                         DatabaseManager& manager,
                                         const char* publicId) ORTHANC_OVERRIDE;
#  endif
#endif

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
    // New primitive since Orthanc 1.5.4
    virtual void GetAllMetadata(std::map<int32_t, std::string>& result,
                                DatabaseManager& manager,
                                int64_t id) ORTHANC_OVERRIDE;
#  endif
#endif

    virtual bool HasCreateInstance() const ORTHANC_OVERRIDE
    {
      // This extension is available in PostgreSQL and MySQL, but is
      // emulated by "CreateInstanceGeneric()" in SQLite
      return false;
    }

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void CreateInstance(OrthancPluginCreateInstanceResult& result,
                                DatabaseManager& manager,
                                const char* hashPatient,
                                const char* hashStudy,
                                const char* hashSeries,
                                const char* hashInstance) ORTHANC_OVERRIDE
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    // This function corresponds to
    // "Orthanc::Compatibility::ICreateInstance::Apply()"
    void CreateInstanceGeneric(OrthancPluginCreateInstanceResult& result,
                               DatabaseManager& manager,
                               const char* hashPatient,
                               const char* hashStudy,
                               const char* hashSeries,
                               const char* hashInstance);
#endif

    bool LookupGlobalIntegerProperty(int& target /*out*/,
                                     DatabaseManager& manager,
                                     const char* serverIdentifier,
                                     int32_t property);

    void SetGlobalIntegerProperty(DatabaseManager& manager,
                                  const char* serverIdentifier,
                                  int32_t property,
                                  int value);

    /**
     * "maxDatabaseRetries" is to handle
     * "OrthancPluginErrorCode_DatabaseCannotSerialize" if there is a
     * collision multiple writers. "countConnections" and
     * "maxDatabaseRetries" are only used if Orthanc >= 1.9.2.
     **/
    static void Register(IndexBackend* backend,
                         size_t countConnections,
                         unsigned int maxDatabaseRetries);

    static void Finalize();

    static DatabaseManager* CreateSingleDatabaseManager(IDatabaseBackend& backend);
  };
}
