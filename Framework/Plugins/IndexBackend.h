/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
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


#pragma once

#include "IDatabaseBackend.h"

#include "../../Resources/Orthanc/Plugins/OrthancPluginException.h"

#include <boost/thread/shared_mutex.hpp>


namespace OrthancDatabases
{
  /**
   * Counterpart of "IndexBackend" in the PostgreSQL plugin, without
   * any SQL. Upstream, this class implements the whole index on top
   * of SQL statements. Here, it only implements the parts that are
   * independent of the database engine (registration, output
   * factory, integer global properties), and provides defaults for
   * the optional capabilities of the Orthanc database SDK: they are
   * all disabled until a subclass enables them.
   *
   * WARNING: This class can be invoked concurrently by several
   * threads (one per connection of the pool).
   **/
  class IndexBackend : public IDatabaseBackend
  {
  private:
    OrthancPluginContext*  context_;
    bool                   readOnly_;

    boost::shared_mutex                                outputFactoryMutex_;
    std::unique_ptr<IDatabaseBackendOutput::IFactory>  outputFactory_;

  protected:
    bool IsReadOnly() const
    {
      return readOnly_;
    }

  public:
    explicit IndexBackend(OrthancPluginContext* context,
                          bool readOnly);

    virtual OrthancPluginContext* GetContext() ORTHANC_OVERRIDE
    {
      return context_;
    }

    virtual void SetOutputFactory(IDatabaseBackendOutput::IFactory* factory) ORTHANC_OVERRIDE;

    virtual IDatabaseBackendOutput* CreateOutput() ORTHANC_OVERRIDE;

    virtual uint32_t GetDatabaseVersion(DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual void UpgradeDatabase(DatabaseManager& manager,
                                 uint32_t  targetVersion,
                                 OrthancPluginStorageArea* storageArea) ORTHANC_OVERRIDE;

    bool LookupGlobalIntegerProperty(int& target /*out*/,
                                     DatabaseManager& manager,
                                     const char* serverIdentifier,
                                     int32_t property);

    void SetGlobalIntegerProperty(DatabaseManager& manager,
                                  const char* serverIdentifier,
                                  int32_t property,
                                  int value);


    /**
     * Optional capabilities, all disabled by default
     **/

    virtual bool HasRevisionsSupport() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasAttachmentCustomDataSupport() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasKeyValueStores() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasQueues() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasReserveQueueValue() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasAuditLogs() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasCreateInstance() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasLabelsSupport() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasAtomicIncrementGlobalProperty() ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasUpdateAndGetStatistics() ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasMeasureLatency() ORTHANC_OVERRIDE
    {
      return false;
    }

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 5)
    virtual bool HasFindSupport() const ORTHANC_OVERRIDE
    {
      return false;
    }

    virtual bool HasExtendedChanges() const ORTHANC_OVERRIDE
    {
      return false;
    }
#endif

    virtual bool HasPerformDbHousekeeping() ORTHANC_OVERRIDE
    {
      return false;
    }


    /**
     * Default implementations of the optional primitives: they are
     * only invoked by the Orthanc core if the corresponding
     * capability is enabled.
     **/

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
    virtual void AddAttachment(DatabaseManager& manager,
                               int64_t id,
                               const OrthancPluginAttachment& attachment,
                               int64_t revision,
                               const std::string& customData) ORTHANC_OVERRIDE;
#endif

    using IDatabaseBackend::AddAttachment;

    virtual void GetChangesExtended(IDatabaseBackendOutput& output,
                                    bool& done /*out*/,
                                    DatabaseManager& manager,
                                    int64_t since,
                                    int64_t to,
                                    const std::set<uint32_t>& changeTypes,
                                    uint32_t limit) ORTHANC_OVERRIDE;

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void CreateInstance(OrthancPluginCreateInstanceResult& result,
                                DatabaseManager& manager,
                                const char* hashPatient,
                                const char* hashStudy,
                                const char* hashSeries,
                                const char* hashInstance) ORTHANC_OVERRIDE;
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

    virtual void AddLabel(DatabaseManager& manager,
                          int64_t resource,
                          const std::string& label) ORTHANC_OVERRIDE;

    virtual void RemoveLabel(DatabaseManager& manager,
                             int64_t resource,
                             const std::string& label) ORTHANC_OVERRIDE;

    virtual void ListLabels(std::list<std::string>& target,
                            DatabaseManager& manager,
                            int64_t resource) ORTHANC_OVERRIDE;

    virtual void ListAllLabels(std::list<std::string>& target,
                               DatabaseManager& manager) ORTHANC_OVERRIDE;

    virtual int64_t IncrementGlobalProperty(DatabaseManager& manager,
                                            const char* serverIdentifier,
                                            int32_t property,
                                            int64_t increment) ORTHANC_OVERRIDE;

    virtual void UpdateAndGetStatistics(DatabaseManager& manager,
                                        int64_t& patientsCount,
                                        int64_t& studiesCount,
                                        int64_t& seriesCount,
                                        int64_t& instancesCount,
                                        int64_t& compressedSize,
                                        int64_t& uncompressedSize) ORTHANC_OVERRIDE;

    virtual uint64_t MeasureLatency(DatabaseManager& manager) ORTHANC_OVERRIDE;

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 5)
    virtual void ExecuteFind(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                             DatabaseManager& manager,
                             const Orthanc::DatabasePluginMessages::Find_Request& request) ORTHANC_OVERRIDE;

    virtual void ExecuteCount(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                              DatabaseManager& manager,
                              const Orthanc::DatabasePluginMessages::Find_Request& request) ORTHANC_OVERRIDE;
#endif

#if ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES == 1
    virtual void StoreKeyValue(DatabaseManager& manager,
                               const std::string& storeId,
                               const std::string& key,
                               const std::string& value) ORTHANC_OVERRIDE;

    virtual void DeleteKeyValue(DatabaseManager& manager,
                                const std::string& storeId,
                                const std::string& key) ORTHANC_OVERRIDE;

    virtual bool GetKeyValue(std::string& value,
                             DatabaseManager& manager,
                             const std::string& storeId,
                             const std::string& key) ORTHANC_OVERRIDE;

    virtual void ListKeysValues(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                DatabaseManager& manager,
                                const Orthanc::DatabasePluginMessages::ListKeysValues_Request& request) ORTHANC_OVERRIDE;
#endif

#if ORTHANC_PLUGINS_HAS_QUEUES == 1
    virtual void EnqueueValue(DatabaseManager& manager,
                              const std::string& queueId,
                              const std::string& value) ORTHANC_OVERRIDE;

    virtual bool DequeueValue(std::string& value,
                              DatabaseManager& manager,
                              const std::string& queueId,
                              bool fromFront) ORTHANC_OVERRIDE;

    virtual uint64_t GetQueueSize(DatabaseManager& manager,
                                  const std::string& queueId) ORTHANC_OVERRIDE;
#endif

#if ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE == 1
    virtual bool ReserveQueueValue(std::string& value,
                                   uint64_t& valueId,
                                   DatabaseManager& manager,
                                   const std::string& queueId,
                                   bool fromFront,
                                   uint32_t reserveTimeout) ORTHANC_OVERRIDE;

    virtual void AcknowledgeQueueValue(DatabaseManager& manager,
                                       const std::string& queueId,
                                       uint64_t valueId) ORTHANC_OVERRIDE;
#endif

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
    virtual void GetAttachmentCustomData(std::string& customData,
                                         DatabaseManager& manager,
                                         const std::string& attachmentUuid) ORTHANC_OVERRIDE;

    virtual void SetAttachmentCustomData(DatabaseManager& manager,
                                         const std::string& attachmentUuid,
                                         const std::string& customData) ORTHANC_OVERRIDE;
#endif

#if ORTHANC_PLUGINS_HAS_AUDIT_LOGS == 1
    virtual void RecordAuditLog(DatabaseManager& manager,
                                const std::string& sourcePlugin,
                                const std::string& userId,
                                OrthancPluginResourceType type,
                                const std::string& resourceId,
                                const std::string& action,
                                const void* logData,
                                uint32_t logDataSize) ORTHANC_OVERRIDE;

    virtual void GetAuditLogs(DatabaseManager& manager,
                              std::list<AuditLog>& logs,
                              const std::string& userIdFilter,
                              const std::string& resourceIdFilter,
                              const std::string& actionFilter,
                              const std::string& fromTsIsoFormat,
                              const std::string& toTsIsoFormat,
                              uint64_t since,
                              uint64_t limit) ORTHANC_OVERRIDE;
#endif

    virtual void PerformDbHousekeeping(DatabaseManager& manager) ORTHANC_OVERRIDE;

    // Invoked once the "OrthancStarted" event has been received
    virtual void SetOrthancStarted()
    {
    }


    /**
     * Primitives for the unit tests only (they are not part of the
     * Orthanc database SDK)
     **/

    virtual uint64_t GetAllResourcesCount(DatabaseManager& manager) = 0;

    virtual uint64_t GetUnprotectedPatientsCount(DatabaseManager& manager) = 0;

    virtual bool GetParentPublicId(std::string& target,
                                   DatabaseManager& manager,
                                   int64_t id) = 0;

    virtual void GetChildren(std::list<std::string>& childrenPublicIds,
                             DatabaseManager& manager,
                             int64_t id) = 0;


    /**
     * "maxDatabaseRetries" is to handle
     * "OrthancPluginErrorCode_DatabaseCannotSerialize" if there is a
     * collision multiple writers.
     **/
    static void Register(IndexBackend* backend,
                         size_t countConnections,
                         bool useDynamicConnectionPool,
                         unsigned int maxDatabaseRetries,
                         unsigned int housekeepingDelaySeconds);

    static void Finalize();

    static DatabaseManager* CreateSingleDatabaseManager(IDatabaseBackend& backend,
                                                        bool hasIdentifierTags,
                                                        const std::list<IdentifierTag>& identifierTags);
  };
}
