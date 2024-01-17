// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "IndexBackend.h"

#include "DatabaseBackendAdapterV2.h"
#include "DatabaseBackendAdapterV3.h"
#include "GlobalProperties.h"

#include <Compatibility.h> // For std::unique_ptr<>
#include <Logging.h>
#include <OrthancException.h>

namespace OrthancDatabases
{
  void IndexBackend::ReadChangesInternal(IDatabaseBackendOutput &output,
                                         bool &done,
                                         DatabaseManager &manager,
                                         uint32_t maxResults)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::ReadExportedResourcesInternal(IDatabaseBackendOutput &output,
                                                   bool &done,
                                                   uint32_t maxResults)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::ClearDeletedFiles(DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::ClearDeletedResources(DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::SignalDeletedFiles(IDatabaseBackendOutput &output,
                                        DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::SignalDeletedResources(IDatabaseBackendOutput &output,
                                            DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  IndexBackend::IndexBackend(OrthancPluginContext *context) : context_(context)
  {
  }

  void IndexBackend::SetOutputFactory(IDatabaseBackendOutput::IFactory *factory)
  {
    boost::unique_lock<boost::shared_mutex> lock(outputFactoryMutex_);

    if (factory == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }
    else if (outputFactory_.get() != NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      outputFactory_.reset(factory);
    }
  }

  IDatabaseBackendOutput *IndexBackend::CreateOutput()
  {
    boost::shared_lock<boost::shared_mutex> lock(outputFactoryMutex_);

    if (outputFactory_.get() == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      return outputFactory_->CreateOutput();
    }
  }

  static void ExecuteAddAttachment(int64_t id,
                                   const OrthancPluginAttachment &attachment)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::AddAttachment(DatabaseManager &manager,
                                   int64_t id,
                                   const OrthancPluginAttachment &attachment,
                                   int64_t revision)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::AttachChild(DatabaseManager &manager,
                                 int64_t parent,
                                 int64_t child)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::ClearChanges(DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::ClearExportedResources(DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::DeleteAttachment(IDatabaseBackendOutput &output,
                                      DatabaseManager &manager,
                                      int64_t id,
                                      int32_t attachment)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::DeleteMetadata(DatabaseManager &manager,
                                    int64_t id,
                                    int32_t metadataType)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::DeleteResource(IDatabaseBackendOutput &output,
                                    DatabaseManager &manager,
                                    int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::GetAllInternalIds(std::list<int64_t> &target,
                                       DatabaseManager &manager,
                                       OrthancPluginResourceType resourceType)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::GetAllPublicIds(std::list<std::string> &target,
                                     DatabaseManager &manager,
                                     OrthancPluginResourceType resourceType)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::GetAllPublicIds(std::list<std::string> &target,
                                     DatabaseManager &manager,
                                     OrthancPluginResourceType resourceType,
                                     uint64_t since,
                                     uint64_t limit)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  /* Use GetOutput().AnswerChange() */
  void IndexBackend::GetChanges(IDatabaseBackendOutput &output,
                                bool &done /*out*/,
                                DatabaseManager &manager,
                                int64_t since,
                                uint32_t maxResults)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::GetChildrenInternalId(std::list<int64_t> &target /*out*/,
                                           DatabaseManager &manager,
                                           int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::GetChildrenPublicId(std::list<std::string> &target /*out*/,
                                         DatabaseManager &manager,
                                         int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  /* Use GetOutput().AnswerExportedResource() */
  void IndexBackend::GetExportedResources(IDatabaseBackendOutput &output,
                                          bool &done /*out*/,
                                          DatabaseManager &manager,
                                          int64_t since,
                                          uint32_t maxResults)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  /* Use GetOutput().AnswerChange() */
  void IndexBackend::GetLastChange(IDatabaseBackendOutput &output,
                                   DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  /* Use GetOutput().AnswerExportedResource() */
  void IndexBackend::GetLastExportedResource(IDatabaseBackendOutput &output,
                                             DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  /* Use GetOutput().AnswerDicomTag() */
  void IndexBackend::GetMainDicomTags(IDatabaseBackendOutput &output,
                                      DatabaseManager &manager,
                                      int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  std::string IndexBackend::GetPublicId(DatabaseManager &manager,
                                        int64_t resourceId)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  uint64_t IndexBackend::GetResourcesCount(DatabaseManager &manager,
                                           OrthancPluginResourceType resourceType)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  OrthancPluginResourceType IndexBackend::GetResourceType(DatabaseManager &manager,
                                                          int64_t resourceId)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  uint64_t IndexBackend::GetTotalCompressedSize(DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  uint64_t IndexBackend::GetTotalUncompressedSize(DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  bool IndexBackend::IsExistingResource(DatabaseManager &manager,
                                        int64_t internalId)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  bool IndexBackend::IsProtectedPatient(DatabaseManager &manager,
                                        int64_t internalId)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::ListAvailableMetadata(std::list<int32_t> &target /*out*/,
                                           DatabaseManager &manager,
                                           int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::ListAvailableAttachments(std::list<int32_t> &target /*out*/,
                                              DatabaseManager &manager,
                                              int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::LogChange(DatabaseManager &manager,
                               int32_t changeType,
                               int64_t resourceId,
                               OrthancPluginResourceType resourceType,
                               const char *date)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::LogExportedResource(DatabaseManager &manager,
                                         const OrthancPluginExportedResource &resource)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  static bool ExecuteLookupAttachment(IDatabaseBackendOutput &output,
                                      int64_t id,
                                      int32_t contentType)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  /* Use GetOutput().AnswerAttachment() */
  bool IndexBackend::LookupAttachment(IDatabaseBackendOutput &output,
                                      int64_t &revision /*out*/,
                                      DatabaseManager &manager,
                                      int64_t id,
                                      int32_t contentType)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  static bool ReadGlobalProperty(std::string &target)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  bool IndexBackend::LookupGlobalProperty(std::string &target /*out*/,
                                          DatabaseManager &manager,
                                          const char *serverIdentifier,
                                          int32_t property)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::LookupIdentifier(std::list<int64_t> &target /*out*/,
                                      DatabaseManager &manager,
                                      OrthancPluginResourceType resourceType,
                                      uint16_t group,
                                      uint16_t element,
                                      OrthancPluginIdentifierConstraint constraint,
                                      const char *value)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::LookupIdentifierRange(std::list<int64_t> &target /*out*/,
                                           DatabaseManager &manager,
                                           OrthancPluginResourceType resourceType,
                                           uint16_t group,
                                           uint16_t element,
                                           const char *start,
                                           const char *end)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  bool IndexBackend::LookupMetadata(std::string &target /*out*/,
                                    int64_t &revision /*out*/,
                                    DatabaseManager &manager,
                                    int64_t id,
                                    int32_t metadataType)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  bool IndexBackend::LookupParent(int64_t &parentId /*out*/,
                                  DatabaseManager &manager,
                                  int64_t resourceId)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  bool IndexBackend::LookupResource(int64_t &id /*out*/,
                                    OrthancPluginResourceType &type /*out*/,
                                    DatabaseManager &manager,
                                    const char *publicId)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  bool IndexBackend::SelectPatientToRecycle(int64_t &internalId /*out*/,
                                            DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  bool IndexBackend::SelectPatientToRecycle(int64_t &internalId /*out*/,
                                            DatabaseManager &manager,
                                            int64_t patientIdToAvoid)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  static void RunSetGlobalPropertyStatement(bool hasServer,
                                            bool hasValue,
                                            const char *serverIdentifier,
                                            int32_t property,
                                            const char *utf8)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::SetGlobalProperty(DatabaseManager &manager,
                                       const char *serverIdentifier,
                                       int32_t property,
                                       const char *utf8)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  static void ExecuteSetTag(int64_t id,
                            uint16_t group,
                            uint16_t element,
                            const char *value)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::SetMainDicomTag(DatabaseManager &manager,
                                     int64_t id,
                                     uint16_t group,
                                     uint16_t element,
                                     const char *value)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::SetIdentifierTag(DatabaseManager &manager,
                                      int64_t id,
                                      uint16_t group,
                                      uint16_t element,
                                      const char *value)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  static void ExecuteSetMetadata(int64_t id,
                                 int32_t metadataType,
                                 const char *value)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::SetMetadata(DatabaseManager &manager,
                                 int64_t id,
                                 int32_t metadataType,
                                 const char *value,
                                 int64_t revision)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  void IndexBackend::SetProtectedPatient(DatabaseManager &manager,
                                         int64_t internalId,
                                         bool isProtected)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  uint32_t IndexBackend::GetDatabaseVersion(DatabaseManager &manager)
  {
    return 6;
  }

  /**
   * Upgrade the database to the specified version of the database
   * schema.  The upgrade script is allowed to make calls to
   * OrthancPluginReconstructMainDicomTags().
   **/
  void IndexBackend::UpgradeDatabase(DatabaseManager &manager,
                                     uint32_t targetVersion,
                                     OrthancPluginStorageArea *storageArea)
  {
    LOG(ERROR) << "Upgrading database is not implemented by this plugin";
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
  }

  void IndexBackend::ClearMainDicomTags(DatabaseManager &manager,
                                        int64_t internalId)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  // For unit testing only!
  uint64_t IndexBackend::GetAllResourcesCount(DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  // For unit testing only!
  uint64_t IndexBackend::GetUnprotectedPatientsCount(DatabaseManager &manager)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  // For unit testing only!
  bool IndexBackend::GetParentPublicId(std::string &target,
                                       DatabaseManager &manager,
                                       int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

  // For unit tests only!
  void IndexBackend::GetChildren(std::list<std::string> &childrenPublicIds,
                                 DatabaseManager &manager,
                                 int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  // New primitive since Orthanc 1.5.2
  void IndexBackend::LookupResources(IDatabaseBackendOutput &output,
                                     DatabaseManager &manager,
                                     const std::vector<Orthanc::DatabaseConstraint> &lookup,
                                     OrthancPluginResourceType queryLevel,
                                     uint32_t limit,
                                     bool requestSomeInstance)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  static void ExecuteSetResourcesContentTags(
      DatabaseManager &manager,
      const std::string &table,
      const std::string &variablePrefix,
      uint32_t count,
      const OrthancPluginResourcesContentTags *tags)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  static void ExecuteSetResourcesContentMetadata(
      DatabaseManager &manager,
      bool hasRevisionsSupport,
      uint32_t count,
      const OrthancPluginResourcesContentMetadata *metadata)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  // New primitive since Orthanc 1.5.2
  void IndexBackend::SetResourcesContent(
      DatabaseManager &manager,
      uint32_t countIdentifierTags,
      const OrthancPluginResourcesContentTags *identifierTags,
      uint32_t countMainDicomTags,
      const OrthancPluginResourcesContentTags *mainDicomTags,
      uint32_t countMetadata,
      const OrthancPluginResourcesContentMetadata *metadata)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }
#endif

  // New primitive since Orthanc 1.5.2
  void IndexBackend::GetChildrenMetadata(std::list<std::string> &target,
                                         DatabaseManager &manager,
                                         int64_t resourceId,
                                         int32_t metadata)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

    // New primitive since Orthanc 1.5.2
  void IndexBackend::TagMostRecentPatient(DatabaseManager& manager,
                                          int64_t patient)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE) // Macro introduced in 1.3.1
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
  // New primitive since Orthanc 1.5.4
  bool IndexBackend::LookupResourceAndParent(int64_t &id,
                                             OrthancPluginResourceType &type,
                                             std::string &parentPublicId,
                                             DatabaseManager &manager,
                                             const char *publicId)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }
#endif
#endif

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE) // Macro introduced in 1.3.1
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
  // New primitive since Orthanc 1.5.4
  void IndexBackend::GetAllMetadata(std::map<int32_t, std::string> &result,
                                    DatabaseManager &manager,
                                    int64_t id)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }
#endif
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  void IndexBackend::CreateInstanceGeneric(OrthancPluginCreateInstanceResult &result,
                                           DatabaseManager &manager,
                                           const char *hashPatient,
                                           const char *hashStudy,
                                           const char *hashSeries,
                                           const char *hashInstance)
  {
    throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
  }
#endif

  void IndexBackend::Register(IndexBackend *backend,
                              size_t countConnections,
                              unsigned int maxDatabaseRetries)
  {
    if (backend == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    bool hasLoadedV3 = false;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE) // Macro introduced in Orthanc 1.3.1
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 9, 2)
    if (OrthancPluginCheckVersionAdvanced(backend->GetContext(), 1, 9, 2) == 1)
    {
      LOG(WARNING) << "The index plugin will use " << countConnections << " connection(s) to the database, "
                   << "and will retry up to " << maxDatabaseRetries << " time(s) in the case of a collision";

      OrthancDatabases::DatabaseBackendAdapterV3::Register(backend, countConnections, maxDatabaseRetries);
      hasLoadedV3 = true;
    }
#endif
#endif

    if (!hasLoadedV3)
    {
      LOG(WARNING) << "Performance warning: Your version of the Orthanc core or SDK doesn't support multiple readers/writers";
      OrthancDatabases::DatabaseBackendAdapterV2::Register(backend);
    }
  }

  bool IndexBackend::LookupGlobalIntegerProperty(int &target,
                                                 DatabaseManager &manager,
                                                 const char *serverIdentifier,
                                                 int32_t property)
  {
    std::string value;

    if (LookupGlobalProperty(value, manager, serverIdentifier, property))
    {
      try
      {
        target = boost::lexical_cast<int>(value);
        return true;
      }
      catch (boost::bad_lexical_cast &)
      {
        LOG(ERROR) << "Corrupted PostgreSQL database";
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
      }
    }
    else
    {
      return false;
    }
  }

  void IndexBackend::SetGlobalIntegerProperty(DatabaseManager &manager,
                                              const char *serverIdentifier,
                                              int32_t property,
                                              int value)
  {
    std::string s = boost::lexical_cast<std::string>(value);
    SetGlobalProperty(manager, serverIdentifier, property, s.c_str());
  }

  void IndexBackend::Finalize()
  {
    OrthancDatabases::DatabaseBackendAdapterV2::Finalize();

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE) // Macro introduced in Orthanc 1.3.1
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 9, 2)
    OrthancDatabases::DatabaseBackendAdapterV3::Finalize();
#endif
#endif
  }

  DatabaseManager *IndexBackend::CreateSingleDatabaseManager(IDatabaseBackend &backend)
  {
    std::unique_ptr<DatabaseManager> manager(new DatabaseManager(backend.CreateDatabaseFactory()));
    backend.ConfigureDatabase(*manager);
    return manager.release();
  }
}
