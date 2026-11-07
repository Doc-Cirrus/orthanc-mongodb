/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/



#pragma once

#include "../Common/DatabaseManager.h"
#include "../Common/DatabasesEnumerations.h"
#include "IDatabaseBackendOutput.h"
#include "DatabaseConstraint.h"   // MongoDB: instead of the SQL-only "ISqlLookupFormatter.h"
#include "IdentifierTag.h"

#include <list>

namespace OrthancDatabases
{
  class IDatabaseBackend : public boost::noncopyable
  {
  public:
    class AuditLog
    {
    private:
      std::string timestamp_;
      std::string sourcePlugin_;
      std::string userId_;
      OrthancPluginResourceType resourceType_;
      std::string resourceId_;
      std::string action_;
      std::string logData_;
      bool hasLogData_;

    public:
      AuditLog(const std::string& timestamp,
               const std::string& sourcePlugin,
               const std::string& userId,
               OrthancPluginResourceType resourceType,
               const std::string& resourceId,
               const std::string& action,
               const std::string& logData,
               bool hasLogData) :
        timestamp_(timestamp),
        sourcePlugin_(sourcePlugin),
        userId_(userId),
        resourceType_(resourceType),
        resourceId_(resourceId),
        action_(action),
        logData_(logData),
        hasLogData_(hasLogData)
      {
      }

      const std::string& GetTimestamp() const
      {
        return timestamp_;
      }

      const std::string& GetSourcePlugin() const
      {
        return sourcePlugin_;
      }

      const std::string& GetUserId() const
      {
        return userId_;
      }

      OrthancPluginResourceType GetResourceType() const
      {
        return resourceType_;
      }

      const std::string& GetResourceId() const
      {
        return resourceId_;
      }

      const std::string& GetAction() const
      {
        return action_;
      }

      const std::string& GetLogData() const
      {
        return logData_;
      }

      bool HasLogData() const
      {
        return hasLogData_;
      }
    };

  public:
    virtual ~IDatabaseBackend()
    {
    }

    virtual OrthancPluginContext* GetContext() = 0;

    virtual IDatabaseFactory* CreateDatabaseFactory() = 0;

    /**
     * This function is invoked once, even if multiple connections are
     * open. It is notably used to update the schema of the database.
     **/
    virtual void ConfigureDatabase(DatabaseManager& database,
                                   bool hasIdentifierTags,
                                   const std::list<IdentifierTag>& identifierTags) = 0;

    virtual void SetOutputFactory(IDatabaseBackendOutput::IFactory* factory) = 0;
                        
    virtual IDatabaseBackendOutput* CreateOutput() = 0;

    virtual bool HasRevisionsSupport() const = 0;

    virtual bool HasAttachmentCustomDataSupport() const = 0;

    virtual bool HasKeyValueStores() const = 0;

    virtual bool HasQueues() const = 0;

    virtual bool HasReserveQueueValue() const = 0;

    virtual bool HasAuditLogs() const = 0;

    virtual void AddAttachment(DatabaseManager& manager,
                               int64_t id,
                               const OrthancPluginAttachment& attachment,
                               int64_t revision) = 0;

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
    // New in Orthanc 1.12.8
    virtual void AddAttachment(DatabaseManager& manager,
                               int64_t id,
                               const OrthancPluginAttachment& attachment,
                               int64_t revision,
                               const std::string& customData) = 0;
#endif

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
                                 int64_t since,
                                 uint32_t limit) = 0;

    /* Use GetOutput().AnswerChange() */
    virtual void GetChanges(IDatabaseBackendOutput& output,
                            bool& done /*out*/,
                            DatabaseManager& manager,
                            int64_t since,
                            uint32_t limit) = 0;

    virtual void GetChangesExtended(IDatabaseBackendOutput& output,
                                    bool& done /*out*/,
                                    DatabaseManager& manager,
                                    int64_t since,
                                    int64_t to,
                                    const std::set<uint32_t>& changeTypes,
                                    uint32_t limit) = 0;

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
                                      uint32_t limit) = 0;

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
                                     OrthancPluginResourceType resourceType,
                                     const char* publicId,
                                     const char* modality,
                                     const char* date,
                                     const char* patientId,
                                     const char* studyInstanceUid,
                                     const char* seriesInstanceUid,
                                     const char* sopInstanceUid) = 0;
    
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
                                 const DatabaseConstraints& lookup,
                                 OrthancPluginResourceType queryLevel,
                                 const std::set<std::string>& labels,     // New in Orthanc 1.12.0
                                 LabelsConstraint labelsConstraint,       // New in Orthanc 1.12.0
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

    // NB: "parentPublicId" must be cleared if the resource has no parent
    virtual bool LookupResourceAndParent(int64_t& id,
                                         OrthancPluginResourceType& type,
                                         std::string& parentPublicId,
                                         DatabaseManager& manager,
                                         const char* publicId) = 0;

    virtual void GetAllMetadata(std::map<int32_t, std::string>& result,
                                DatabaseManager& manager,
                                int64_t id) = 0;

    // New in Orthanc 1.12.0
    virtual bool HasLabelsSupport() const = 0;

    // New in Orthanc 1.12.0
    virtual void AddLabel(DatabaseManager& manager,
                          int64_t resource,
                          const std::string& label) = 0;

    // New in Orthanc 1.12.0
    virtual void RemoveLabel(DatabaseManager& manager,
                             int64_t resource,
                             const std::string& label) = 0;

    // New in Orthanc 1.12.0
    virtual void ListLabels(std::list<std::string>& target,
                            DatabaseManager& manager,
                            int64_t resource) = 0;

    // New in Orthanc 1.12.0
    virtual void ListAllLabels(std::list<std::string>& target,
                               DatabaseManager& manager) = 0;

    // New in Orthanc 1.12.3
    virtual bool HasAtomicIncrementGlobalProperty() = 0;

    // New in Orthanc 1.12.3
    virtual int64_t IncrementGlobalProperty(DatabaseManager& manager,
                                            const char* serverIdentifier,
                                            int32_t property,
                                            int64_t increment) = 0;

    // New in Orthanc 1.12.3
    virtual bool HasUpdateAndGetStatistics() = 0;

    // New in Orthanc 1.12.3
    virtual void UpdateAndGetStatistics(DatabaseManager& manager,
                                        int64_t& patientsCount,
                                        int64_t& studiesCount,
                                        int64_t& seriesCount,
                                        int64_t& instancesCount,
                                        int64_t& compressedSize,
                                        int64_t& uncompressedSize) = 0;

    // New in Orthanc 1.12.3
    virtual bool HasMeasureLatency() = 0;

    // New in Orthanc 1.12.3
    virtual uint64_t MeasureLatency(DatabaseManager& manager) = 0;

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 5)
    virtual bool HasFindSupport() const = 0;
    virtual bool HasExtendedChanges() const = 0;

    // New in Orthanc 1.12.5
    virtual void ExecuteFind(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                             DatabaseManager& manager,
                             const Orthanc::DatabasePluginMessages::Find_Request& request) = 0;

    // New in Orthanc 1.12.5
    virtual void ExecuteCount(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                              DatabaseManager& manager,
                              const Orthanc::DatabasePluginMessages::Find_Request& request) = 0;
#endif

#if ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES == 1
    virtual void StoreKeyValue(DatabaseManager& manager,
                               const std::string& storeId,
                               const std::string& key,
                               const std::string& value) = 0;

    virtual void DeleteKeyValue(DatabaseManager& manager,
                                const std::string& storeId,
                                const std::string& key) = 0;

    virtual bool GetKeyValue(std::string& value,
                             DatabaseManager& manager,
                             const std::string& storeId,
                             const std::string& key) = 0;

    virtual void ListKeysValues(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                DatabaseManager& manager,
                                const Orthanc::DatabasePluginMessages::ListKeysValues_Request& request) = 0;
#endif

#if ORTHANC_PLUGINS_HAS_QUEUES == 1
    virtual void EnqueueValue(DatabaseManager& manager,
                              const std::string& queueId,
                              const std::string& value) = 0;

    virtual bool DequeueValue(std::string& value,
                              DatabaseManager& manager,
                              const std::string& queueId,
                              bool fromFront) = 0;

    virtual uint64_t GetQueueSize(DatabaseManager& manager,
                                  const std::string& queueId) = 0;
#endif

#if ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE == 1
    virtual bool ReserveQueueValue(std::string& value,
                                   uint64_t& valueId,
                                   DatabaseManager& manager,
                                   const std::string& queueId,
                                   bool fromFront,
                                   uint32_t reserveTimeout) = 0;

    virtual void AcknowledgeQueueValue(DatabaseManager& manager,
                                       const std::string& queueId,
                                       uint64_t valueId) = 0;
#endif

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
    virtual void GetAttachmentCustomData(std::string& customData,
                                         DatabaseManager& manager,
                                         const std::string& attachmentUuid) = 0;

    virtual void SetAttachmentCustomData(DatabaseManager& manager,
                                         const std::string& attachmentUuid,
                                         const std::string& customData) = 0;
#endif

#if ORTHANC_PLUGINS_HAS_AUDIT_LOGS == 1
    virtual void RecordAuditLog(DatabaseManager& manager,
                                const std::string& sourcePlugin,
                                const std::string& userId,
                                OrthancPluginResourceType type,
                                const std::string& resourceId,
                                const std::string& action,
                                const void* logData,
                                uint32_t logDataSize) = 0;

    virtual void GetAuditLogs(DatabaseManager& manager,
                              std::list<AuditLog>& logs,
                              const std::string& userIdFilter,
                              const std::string& resourceIdFilter,
                              const std::string& actionFilter,
                              const std::string& fromTsIsoFormat,
                              const std::string& toTsIsoFormat,
                              uint64_t since,
                              uint64_t limit) = 0;
#endif

    virtual bool HasPerformDbHousekeeping() = 0;

    virtual void PerformDbHousekeeping(DatabaseManager& manager) = 0;
  };
}
