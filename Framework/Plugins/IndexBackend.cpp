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


#include "IndexBackend.h"

#include "DatabaseBackendAdapterV4.h"
#include "GlobalProperties.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>
#include <OrthancException.h>

#include <boost/lexical_cast.hpp>


#define THROW_NOT_IMPLEMENTED(primitive)                                  \
  throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented,     \
                                  "Primitive not supported by this database back-end: " primitive)


namespace OrthancDatabases
{
  IndexBackend::IndexBackend(OrthancPluginContext* context,
                             bool readOnly) :
    context_(context),
    readOnly_(readOnly)
  {
  }


  void IndexBackend::SetOutputFactory(IDatabaseBackendOutput::IFactory* factory)
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


  IDatabaseBackendOutput* IndexBackend::CreateOutput()
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


  uint32_t IndexBackend::GetDatabaseVersion(DatabaseManager& manager)
  {
    // Create a read-only, explicit transaction to read the database version
    DatabaseManager::Transaction transaction(manager, TransactionType_ReadOnly);

    std::string version = "unknown";

    if (LookupGlobalProperty(version, manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseSchemaVersion))
    {
      try
      {
        return boost::lexical_cast<unsigned int>(version);
      }
      catch (boost::bad_lexical_cast&)
      {
      }
    }

    LOG(ERROR) << "The database is corrupted. Drop it manually for Orthanc to recreate it";
    throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
  }


  void IndexBackend::UpgradeDatabase(DatabaseManager& manager,
                                     uint32_t  targetVersion,
                                     OrthancPluginStorageArea* storageArea)
  {
    LOG(ERROR) << "Upgrading database is not implemented by this plugin";
    ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
  }


  bool IndexBackend::LookupGlobalIntegerProperty(int& target,
                                                 DatabaseManager& manager,
                                                 const char* serverIdentifier,
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
      catch (boost::bad_lexical_cast&)
      {
        LOG(ERROR) << "Corrupted database";
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
      }
    }
    else
    {
      return false;
    }
  }


  void IndexBackend::SetGlobalIntegerProperty(DatabaseManager& manager,
                                              const char* serverIdentifier,
                                              int32_t property,
                                              int value)
  {
    std::string s = boost::lexical_cast<std::string>(value);
    SetGlobalProperty(manager, serverIdentifier, property, s.c_str());
  }


#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
  void IndexBackend::AddAttachment(DatabaseManager& manager,
                                   int64_t id,
                                   const OrthancPluginAttachment& attachment,
                                   int64_t revision,
                                   const std::string& customData)
  {
    if (customData.empty())
    {
      AddAttachment(manager, id, attachment, revision);
    }
    else
    {
      THROW_NOT_IMPLEMENTED("AddAttachment with custom data");
    }
  }
#endif


  void IndexBackend::GetChangesExtended(IDatabaseBackendOutput& output,
                                        bool& done,
                                        DatabaseManager& manager,
                                        int64_t since,
                                        int64_t to,
                                        const std::set<uint32_t>& changeTypes,
                                        uint32_t limit)
  {
    THROW_NOT_IMPLEMENTED("GetChangesExtended");
  }


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  void IndexBackend::CreateInstance(OrthancPluginCreateInstanceResult& result,
                                    DatabaseManager& manager,
                                    const char* hashPatient,
                                    const char* hashStudy,
                                    const char* hashSeries,
                                    const char* hashInstance)
  {
    THROW_NOT_IMPLEMENTED("CreateInstance");
  }
#endif


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  void IndexBackend::CreateInstanceGeneric(OrthancPluginCreateInstanceResult& result,
                                           DatabaseManager& manager,
                                           const char* hashPatient,
                                           const char* hashStudy,
                                           const char* hashSeries,
                                           const char* hashInstance)
  {
    // Check out "OrthancServer/Sources/Database/Compatibility/ICreateInstance.cpp"
    
    {
      OrthancPluginResourceType type;
      int64_t tmp;
        
      if (LookupResource(tmp, type, manager, hashInstance))
      {
        // The instance already exists
        assert(type == OrthancPluginResourceType_Instance);
        result.instanceId = tmp;
        result.isNewInstance = false;
        return;
      }
    }

    result.instanceId = CreateResource(manager, hashInstance, OrthancPluginResourceType_Instance);
    result.isNewInstance = true;

    result.isNewPatient = false;
    result.isNewStudy = false;
    result.isNewSeries = false;
    result.patientId = -1;
    result.studyId = -1;
    result.seriesId = -1;
      
    // Detect up to which level the patient/study/series/instance
    // hierarchy must be created

    {
      OrthancPluginResourceType dummy;

      if (LookupResource(result.seriesId, dummy, manager, hashSeries))
      {
        assert(dummy == OrthancPluginResourceType_Series);
        // The patient, the study and the series already exist

        bool ok = (LookupResource(result.patientId, dummy, manager, hashPatient) &&
                   LookupResource(result.studyId, dummy, manager, hashStudy));
        (void) ok;  // Remove warning about unused variable in release builds
        assert(ok);
      }
      else if (LookupResource(result.studyId, dummy, manager, hashStudy))
      {
        assert(dummy == OrthancPluginResourceType_Study);

        // New series: The patient and the study already exist
        result.isNewSeries = true;

        bool ok = LookupResource(result.patientId, dummy, manager, hashPatient);
        (void) ok;  // Remove warning about unused variable in release builds
        assert(ok);
      }
      else if (LookupResource(result.patientId, dummy, manager, hashPatient))
      {
        assert(dummy == OrthancPluginResourceType_Patient);

        // New study and series: The patient already exist
        result.isNewStudy = true;
        result.isNewSeries = true;
      }
      else
      {
        // New patient, study and series: Nothing exists
        result.isNewPatient = true;
        result.isNewStudy = true;
        result.isNewSeries = true;
      }
    }

    // Create the series if needed
    if (result.isNewSeries)
    {
      result.seriesId = CreateResource(manager, hashSeries, OrthancPluginResourceType_Series);
    }

    // Create the study if needed
    if (result.isNewStudy)
    {
      result.studyId = CreateResource(manager, hashStudy, OrthancPluginResourceType_Study);
    }

    // Create the patient if needed
    if (result.isNewPatient)
    {
      result.patientId = CreateResource(manager, hashPatient, OrthancPluginResourceType_Patient);
    }

    // Create the parent-to-child links
    AttachChild(manager, result.seriesId, result.instanceId);

    if (result.isNewSeries)
    {
      AttachChild(manager, result.studyId, result.seriesId);
    }

    if (result.isNewStudy)
    {
      AttachChild(manager, result.patientId, result.studyId);
    }

    TagMostRecentPatient(manager, result.patientId);
      
    // Sanity checks
    assert(result.patientId != -1);
    assert(result.studyId != -1);
    assert(result.seriesId != -1);
    assert(result.instanceId != -1);
  }
#endif


  void IndexBackend::AddLabel(DatabaseManager& manager,
                              int64_t resource,
                              const std::string& label)
  {
    THROW_NOT_IMPLEMENTED("AddLabel");
  }


  void IndexBackend::RemoveLabel(DatabaseManager& manager,
                                 int64_t resource,
                                 const std::string& label)
  {
    THROW_NOT_IMPLEMENTED("RemoveLabel");
  }


  void IndexBackend::ListLabels(std::list<std::string>& target,
                                DatabaseManager& manager,
                                int64_t resource)
  {
    THROW_NOT_IMPLEMENTED("ListLabels");
  }


  void IndexBackend::ListAllLabels(std::list<std::string>& target,
                                   DatabaseManager& manager)
  {
    THROW_NOT_IMPLEMENTED("ListAllLabels");
  }


  int64_t IndexBackend::IncrementGlobalProperty(DatabaseManager& manager,
                                                const char* serverIdentifier,
                                                int32_t property,
                                                int64_t increment)
  {
    THROW_NOT_IMPLEMENTED("IncrementGlobalProperty");
  }


  void IndexBackend::UpdateAndGetStatistics(DatabaseManager& manager,
                                            int64_t& patientsCount,
                                            int64_t& studiesCount,
                                            int64_t& seriesCount,
                                            int64_t& instancesCount,
                                            int64_t& compressedSize,
                                            int64_t& uncompressedSize)
  {
    THROW_NOT_IMPLEMENTED("UpdateAndGetStatistics");
  }


  uint64_t IndexBackend::MeasureLatency(DatabaseManager& manager)
  {
    THROW_NOT_IMPLEMENTED("MeasureLatency");
  }


#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 5)
  void IndexBackend::ExecuteFind(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                 DatabaseManager& manager,
                                 const Orthanc::DatabasePluginMessages::Find_Request& request)
  {
    THROW_NOT_IMPLEMENTED("ExecuteFind");
  }


  void IndexBackend::ExecuteCount(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                  DatabaseManager& manager,
                                  const Orthanc::DatabasePluginMessages::Find_Request& request)
  {
    THROW_NOT_IMPLEMENTED("ExecuteCount");
  }
#endif


#if ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES == 1
  void IndexBackend::StoreKeyValue(DatabaseManager& manager,
                                   const std::string& storeId,
                                   const std::string& key,
                                   const std::string& value)
  {
    THROW_NOT_IMPLEMENTED("StoreKeyValue");
  }


  void IndexBackend::DeleteKeyValue(DatabaseManager& manager,
                                    const std::string& storeId,
                                    const std::string& key)
  {
    THROW_NOT_IMPLEMENTED("DeleteKeyValue");
  }


  bool IndexBackend::GetKeyValue(std::string& value,
                                 DatabaseManager& manager,
                                 const std::string& storeId,
                                 const std::string& key)
  {
    THROW_NOT_IMPLEMENTED("GetKeyValue");
  }


  void IndexBackend::ListKeysValues(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                    DatabaseManager& manager,
                                    const Orthanc::DatabasePluginMessages::ListKeysValues_Request& request)
  {
    THROW_NOT_IMPLEMENTED("ListKeysValues");
  }
#endif


#if ORTHANC_PLUGINS_HAS_QUEUES == 1
  void IndexBackend::EnqueueValue(DatabaseManager& manager,
                                  const std::string& queueId,
                                  const std::string& value)
  {
    THROW_NOT_IMPLEMENTED("EnqueueValue");
  }


  bool IndexBackend::DequeueValue(std::string& value,
                                  DatabaseManager& manager,
                                  const std::string& queueId,
                                  bool fromFront)
  {
    THROW_NOT_IMPLEMENTED("DequeueValue");
  }


  uint64_t IndexBackend::GetQueueSize(DatabaseManager& manager,
                                      const std::string& queueId)
  {
    THROW_NOT_IMPLEMENTED("GetQueueSize");
  }
#endif


#if ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE == 1
  bool IndexBackend::ReserveQueueValue(std::string& value,
                                       uint64_t& valueId,
                                       DatabaseManager& manager,
                                       const std::string& queueId,
                                       bool fromFront,
                                       uint32_t reserveTimeout)
  {
    THROW_NOT_IMPLEMENTED("ReserveQueueValue");
  }


  void IndexBackend::AcknowledgeQueueValue(DatabaseManager& manager,
                                           const std::string& queueId,
                                           uint64_t valueId)
  {
    THROW_NOT_IMPLEMENTED("AcknowledgeQueueValue");
  }
#endif


#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
  void IndexBackend::GetAttachmentCustomData(std::string& customData,
                                             DatabaseManager& manager,
                                             const std::string& attachmentUuid)
  {
    THROW_NOT_IMPLEMENTED("GetAttachmentCustomData");
  }


  void IndexBackend::SetAttachmentCustomData(DatabaseManager& manager,
                                             const std::string& attachmentUuid,
                                             const std::string& customData)
  {
    THROW_NOT_IMPLEMENTED("SetAttachmentCustomData");
  }
#endif


#if ORTHANC_PLUGINS_HAS_AUDIT_LOGS == 1
  void IndexBackend::RecordAuditLog(DatabaseManager& manager,
                                    const std::string& sourcePlugin,
                                    const std::string& userId,
                                    OrthancPluginResourceType type,
                                    const std::string& resourceId,
                                    const std::string& action,
                                    const void* logData,
                                    uint32_t logDataSize)
  {
    THROW_NOT_IMPLEMENTED("RecordAuditLog");
  }


  void IndexBackend::GetAuditLogs(DatabaseManager& manager,
                                  std::list<AuditLog>& logs,
                                  const std::string& userIdFilter,
                                  const std::string& resourceIdFilter,
                                  const std::string& actionFilter,
                                  const std::string& fromTsIsoFormat,
                                  const std::string& toTsIsoFormat,
                                  uint64_t since,
                                  uint64_t limit)
  {
    THROW_NOT_IMPLEMENTED("GetAuditLogs");
  }
#endif


  void IndexBackend::PerformDbHousekeeping(DatabaseManager& manager)
  {
    THROW_NOT_IMPLEMENTED("PerformDbHousekeeping");
  }


  void IndexBackend::Register(IndexBackend* backend,
                              size_t countConnections,
                              bool useDynamicConnectionPool,
                              unsigned int maxDatabaseRetries,
                              unsigned int housekeepingDelaySeconds)
  {
    if (backend == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    LOG(WARNING) << "The index plugin will use " << countConnections << " connection(s) to the database, "
                 << "and will retry up to " << maxDatabaseRetries << " time(s) in the case of a collision";

    if (OrthancPluginCheckVersionAdvanced(backend->GetContext(), 1, 13, 0) == 1)
    {
      DatabaseBackendAdapterV4::Register(backend, countConnections, useDynamicConnectionPool, maxDatabaseRetries, housekeepingDelaySeconds);
    }
    else
    {
      delete backend;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_Plugin,
                                      "The MongoDB index plugin requires Orthanc >= 1.13.0");
    }
  }


  void IndexBackend::Finalize()
  {
    DatabaseBackendAdapterV4::Finalize();
  }


  DatabaseManager* IndexBackend::CreateSingleDatabaseManager(IDatabaseBackend& backend,
                                                             bool hasIdentifierTags,
                                                             const std::list<IdentifierTag>& identifierTags)
  {
    std::unique_ptr<DatabaseManager> manager(new DatabaseManager(backend.CreateDatabaseFactory()));
    backend.ConfigureDatabase(*manager, hasIdentifierTags, identifierTags);
    return manager.release();
  }
}
