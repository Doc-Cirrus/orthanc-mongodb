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


#include "MongoDBIndex.h"

#include "MongoDBSchema.h"
#include "Index/MongoDBAttachments.h"
#include "Index/MongoDBAuditLogs.h"
#include "Index/MongoDBChanges.h"
#include "Index/MongoDBExportedResources.h"
#include "Index/MongoDBFind.h"
#include "Index/MongoDBGlobalProperties.h"
#include "Index/MongoDBKeyValueStores.h"
#include "Index/MongoDBLabels.h"
#include "Index/MongoDBLookup.h"
#include "Index/MongoDBMainDicomTags.h"
#include "Index/MongoDBMetadata.h"
#include "Index/MongoDBPatientRecycling.h"
#include "Index/MongoDBQueues.h"
#include "Index/MongoDBResources.h"
#include "Index/MongoDBStatistics.h"

#include <Logging.h>
#include <OrthancException.h>

#include <boost/date_time/posix_time/posix_time.hpp>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  MongoDBIndex::MongoDBIndex(OrthancPluginContext* context,
                             const MongoDBParameters& parameters,
                             bool readOnly) :
    IndexBackend(context, readOnly),
    parameters_(parameters),
    orthancStarted_(false),
    statisticsAttempts_(0)
  {
    if (parameters_.GetConnectionUri().empty())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  void MongoDBIndex::AddLabel(DatabaseManager& manager,
                              int64_t resource,
                              const std::string& label)
  {
    MongoDBLabels(manager).AddLabel(resource, label);
  }


  void MongoDBIndex::RemoveLabel(DatabaseManager& manager,
                                 int64_t resource,
                                 const std::string& label)
  {
    MongoDBLabels(manager).RemoveLabel(resource, label);
  }


  void MongoDBIndex::ListLabels(std::list<std::string>& target,
                                DatabaseManager& manager,
                                int64_t resource)
  {
    MongoDBLabels(manager).ListLabels(target, resource);
  }


  void MongoDBIndex::ListAllLabels(std::list<std::string>& target,
                                   DatabaseManager& manager)
  {
    MongoDBLabels(manager).ListAllLabels(target);
  }


  void MongoDBIndex::StoreKeyValue(DatabaseManager& manager,
                                   const std::string& storeId,
                                   const std::string& key,
                                   const std::string& value)
  {
    MongoDBKeyValueStores(manager).StoreKeyValue(storeId, key, value);
  }


  void MongoDBIndex::DeleteKeyValue(DatabaseManager& manager,
                                    const std::string& storeId,
                                    const std::string& key)
  {
    MongoDBKeyValueStores(manager).DeleteKeyValue(storeId, key);
  }


  bool MongoDBIndex::GetKeyValue(std::string& value,
                                 DatabaseManager& manager,
                                 const std::string& storeId,
                                 const std::string& key)
  {
    return MongoDBKeyValueStores(manager).GetKeyValue(value, storeId, key);
  }


  void MongoDBIndex::ListKeysValues(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                    DatabaseManager& manager,
                                    const Orthanc::DatabasePluginMessages::ListKeysValues_Request& request)
  {
    MongoDBKeyValueStores(manager).ListKeysValues(response, request);
  }


  void MongoDBIndex::EnqueueValue(DatabaseManager& manager,
                                  const std::string& queueId,
                                  const std::string& value)
  {
    MongoDBQueues(manager).EnqueueValue(queueId, value);
  }


  bool MongoDBIndex::DequeueValue(std::string& value,
                                  DatabaseManager& manager,
                                  const std::string& queueId,
                                  bool fromFront)
  {
    return MongoDBQueues(manager).DequeueValue(value, queueId, fromFront);
  }


  uint64_t MongoDBIndex::GetQueueSize(DatabaseManager& manager,
                                      const std::string& queueId)
  {
    return MongoDBQueues(manager).GetQueueSize(queueId);
  }


  bool MongoDBIndex::ReserveQueueValue(std::string& value,
                                       uint64_t& valueId,
                                       DatabaseManager& manager,
                                       const std::string& queueId,
                                       bool fromFront,
                                       uint32_t reserveTimeout)
  {
    return MongoDBQueues(manager).ReserveQueueValue(value, valueId, queueId, fromFront, reserveTimeout);
  }


  void MongoDBIndex::AcknowledgeQueueValue(DatabaseManager& manager,
                                           const std::string& queueId,
                                           uint64_t valueId)
  {
    MongoDBQueues(manager).AcknowledgeQueueValue(queueId, valueId);
  }


  void MongoDBIndex::RecordAuditLog(DatabaseManager& manager,
                                    const std::string& sourcePlugin,
                                    const std::string& userId,
                                    OrthancPluginResourceType type,
                                    const std::string& resourceId,
                                    const std::string& action,
                                    const void* logData,
                                    uint32_t logDataSize)
  {
    MongoDBAuditLogs(manager).RecordAuditLog(sourcePlugin, userId, type, resourceId, action, logData, logDataSize);
  }


  void MongoDBIndex::GetAuditLogs(DatabaseManager& manager,
                                  std::list<AuditLog>& logs,
                                  const std::string& userIdFilter,
                                  const std::string& resourceIdFilter,
                                  const std::string& actionFilter,
                                  const std::string& fromTsIsoFormat,
                                  const std::string& toTsIsoFormat,
                                  uint64_t since,
                                  uint64_t limit)
  {
    MongoDBAuditLogs(manager).GetAuditLogs(logs, userIdFilter, resourceIdFilter, actionFilter,
                                           fromTsIsoFormat, toTsIsoFormat, since, limit);
  }


  int64_t MongoDBIndex::IncrementGlobalProperty(DatabaseManager& manager,
                                                const char* serverIdentifier,
                                                int32_t property,
                                                int64_t increment)
  {
    return MongoDBGlobalProperties(manager).IncrementGlobalProperty(serverIdentifier, property, increment);
  }


  void MongoDBIndex::UpdateAndGetStatistics(DatabaseManager& manager,
                                            int64_t& patientsCount,
                                            int64_t& studiesCount,
                                            int64_t& seriesCount,
                                            int64_t& instancesCount,
                                            int64_t& compressedSize,
                                            int64_t& uncompressedSize)
  {
    MongoDBStatistics statistics(manager);
    statistics.Consolidate();

    MongoDBStatisticsValues values;
    statistics.GetValues(values);

    patientsCount = values.counts_[OrthancPluginResourceType_Patient];
    studiesCount = values.counts_[OrthancPluginResourceType_Study];
    seriesCount = values.counts_[OrthancPluginResourceType_Series];
    instancesCount = values.counts_[OrthancPluginResourceType_Instance];
    compressedSize = values.compressedSize_;
    uncompressedSize = values.uncompressedSize_;
  }


  uint64_t MongoDBIndex::MeasureLatency(DatabaseManager& manager)
  {
    // The mean duration of a round trip to the server, in microseconds
    static const unsigned int COUNT = 5;

    mongocxx::database database = MongoDBDatabase::GetDatabase(manager).GetDatabase();

    try
    {
      database.run_command(make_document(kvp("ping", 1)));  // Warm-up

      const boost::posix_time::ptime start = boost::posix_time::microsec_clock::universal_time();

      for (unsigned int i = 0; i < COUNT; i++)
      {
        database.run_command(make_document(kvp("ping", 1)));
      }

      return static_cast<uint64_t>(
        (boost::posix_time::microsec_clock::universal_time() - start).total_microseconds()) / COUNT;
    }
    catch (mongocxx::exception& e)
    {
      MongoDBDatabase::ThrowException(e);
    }
  }


  void MongoDBIndex::PerformDbHousekeeping(DatabaseManager& manager)
  {
    // As in the PostgreSQL plugin, the full scans wait until Orthanc has started
    if (!orthancStarted_)
    {
      return;
    }

    /**
     * Without snapshots (standalone server), the statistics are only
     * computed if no change is written during the scan. After a few
     * failures on a busy server, they are computed anyway.
     **/
    static const unsigned int MAX_ATTEMPTS = 10;

    try
    {
      MongoDBStatistics statistics(manager);

      if (!statistics.ComputeTotals(statisticsAttempts_ >= MAX_ATTEMPTS))
      {
        if (statisticsAttempts_++ == 0)
        {
          LOG(WARNING) << "MongoDB: the statistics are not computed yet, as the database is changing";
        }

        return;
      }

      DatabaseManager::Transaction transaction(manager, TransactionType_ReadWrite);
      statistics.Consolidate();
      transaction.Commit();
    }
    catch (Orthanc::OrthancException& e)
    {
      if (e.GetErrorCode() != Orthanc::ErrorCode_DatabaseCannotSerialize)
      {
        throw;
      }

      // A concurrent consolidation: the next housekeeping retries
    }
  }


  IDatabaseFactory* MongoDBIndex::CreateDatabaseFactory()
  {
    boost::mutex::scoped_lock lock(poolMutex_);

    if (pool_.get() == NULL)
    {
      pool_ = MongoDBDatabase::CreatePool(parameters_);
    }

    return MongoDBDatabase::CreateDatabaseFactory(pool_);
  }


  void MongoDBIndex::ConfigureDatabase(DatabaseManager& manager,
                                       bool hasIdentifierTags,
                                       const std::list<IdentifierTag>& identifierTags)
  {
    uint32_t expectedVersion = MongoDBSchema::SCHEMA_VERSION;

    if (GetContext())   // "GetContext()" can possibly be NULL in the unit tests
    {
      expectedVersion = OrthancPluginGetExpectedDatabaseVersion(GetContext());
    }

    MongoDBSchema::Configure(manager, parameters_, IsReadOnly(), expectedVersion);
  }


  void MongoDBIndex::AddAttachment(DatabaseManager& manager,
                                   int64_t id,
                                   const OrthancPluginAttachment& attachment,
                                   int64_t revision)
  {
    MongoDBAttachments(manager).AddAttachment(id, attachment, revision, "");
  }


  void MongoDBIndex::AddAttachment(DatabaseManager& manager,
                                   int64_t id,
                                   const OrthancPluginAttachment& attachment,
                                   int64_t revision,
                                   const std::string& customData)
  {
    MongoDBAttachments(manager).AddAttachment(id, attachment, revision, customData);
  }


  void MongoDBIndex::GetAttachmentCustomData(std::string& customData,
                                             DatabaseManager& manager,
                                             const std::string& attachmentUuid)
  {
    MongoDBAttachments(manager).GetCustomData(customData, attachmentUuid);
  }


  void MongoDBIndex::SetAttachmentCustomData(DatabaseManager& manager,
                                             const std::string& attachmentUuid,
                                             const std::string& customData)
  {
    MongoDBAttachments(manager).SetCustomData(attachmentUuid, customData);
  }


  void MongoDBIndex::AttachChild(DatabaseManager& manager,
                                 int64_t parent,
                                 int64_t child)
  {
    MongoDBResources(manager).AttachChild(parent, child);
  }


  void MongoDBIndex::ClearChanges(DatabaseManager& manager)
  {
    MongoDBChanges(manager).ClearChanges();
  }


  void MongoDBIndex::ClearExportedResources(DatabaseManager& manager)
  {
    MongoDBExportedResources(manager).ClearExportedResources();
  }


  void MongoDBIndex::DeleteAttachment(IDatabaseBackendOutput& output,
                                      DatabaseManager& manager,
                                      int64_t id,
                                      int32_t attachment)
  {
    MongoDBAttachments(manager).DeleteAttachment(output, id, attachment);
  }


  void MongoDBIndex::DeleteMetadata(DatabaseManager& manager,
                                    int64_t id,
                                    int32_t metadataType)
  {
    MongoDBMetadata(manager).DeleteMetadata(id, metadataType);
  }


  void MongoDBIndex::DeleteResource(IDatabaseBackendOutput& output,
                                    DatabaseManager& manager,
                                    int64_t id)
  {
    MongoDBResources(manager).DeleteResource(output, id);
  }


  void MongoDBIndex::GetAllInternalIds(std::list<int64_t>& target,
                                       DatabaseManager& manager,
                                       OrthancPluginResourceType resourceType)
  {
    MongoDBResources(manager).GetAllInternalIds(target, resourceType);
  }


  void MongoDBIndex::GetAllPublicIds(std::list<std::string>& target,
                                     DatabaseManager& manager,
                                     OrthancPluginResourceType resourceType)
  {
    MongoDBResources(manager).GetAllPublicIds(target, resourceType);
  }


  void MongoDBIndex::GetAllPublicIds(std::list<std::string>& target,
                                     DatabaseManager& manager,
                                     OrthancPluginResourceType resourceType,
                                     int64_t since,
                                     uint32_t limit)
  {
    MongoDBResources(manager).GetAllPublicIds(target, resourceType, since, limit);
  }


  void MongoDBIndex::GetChanges(IDatabaseBackendOutput& output,
                                bool& done /*out*/,
                                DatabaseManager& manager,
                                int64_t since,
                                uint32_t maxResults)
  {
    MongoDBChanges(manager).GetChanges(output, done, since, maxResults);
  }

  void MongoDBIndex::GetChangesExtended(IDatabaseBackendOutput& output,
                                        bool& done /*out*/,
                                        DatabaseManager& manager,
                                        int64_t since,
                                        int64_t to,
                                        const std::set<uint32_t>& changeTypes,
                                        uint32_t limit)
  {
    MongoDBChanges(manager).GetChangesExtended(output, done, since, to, changeTypes, limit);
  }



  void MongoDBIndex::GetChildrenInternalId(std::list<int64_t>& target /*out*/,
                                           DatabaseManager& manager,
                                           int64_t id)
  {
    MongoDBResources(manager).GetChildrenInternalId(target, id);
  }


  void MongoDBIndex::GetChildrenPublicId(std::list<std::string>& target /*out*/,
                                         DatabaseManager& manager,
                                         int64_t id)
  {
    MongoDBResources(manager).GetChildrenPublicId(target, id);
  }


  void MongoDBIndex::GetExportedResources(IDatabaseBackendOutput& output,
                                          bool& done /*out*/,
                                          DatabaseManager& manager,
                                          int64_t since,
                                          uint32_t maxResults)
  {
    MongoDBExportedResources(manager).GetExportedResources(output, done, since, maxResults);
  }


  void MongoDBIndex::GetLastChange(IDatabaseBackendOutput& output,
                                   DatabaseManager& manager)
  {
    MongoDBChanges(manager).GetLastChange(output);
  }


  void MongoDBIndex::GetLastExportedResource(IDatabaseBackendOutput& output,
                                             DatabaseManager& manager)
  {
    MongoDBExportedResources(manager).GetLastExportedResource(output);
  }


  void MongoDBIndex::GetMainDicomTags(IDatabaseBackendOutput& output,
                                      DatabaseManager& manager,
                                      int64_t id)
  {
    MongoDBMainDicomTags(manager).GetMainDicomTags(output, id);
  }


  std::string MongoDBIndex::GetPublicId(DatabaseManager& manager,
                                        int64_t resourceId)
  {
    return MongoDBResources(manager).GetPublicId(resourceId);
  }


  uint64_t MongoDBIndex::GetResourcesCount(DatabaseManager& manager,
                                           OrthancPluginResourceType resourceType)
  {
    if (static_cast<int32_t>(resourceType) < 0 ||
        static_cast<int32_t>(resourceType) > 3)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    MongoDBStatisticsValues values;
    MongoDBStatistics(manager).GetValues(values);
    return static_cast<uint64_t>(values.counts_[resourceType]);
  }


  OrthancPluginResourceType MongoDBIndex::GetResourceType(DatabaseManager& manager,
                                                          int64_t resourceId)
  {
    return MongoDBResources(manager).GetResourceType(resourceId);
  }


  uint64_t MongoDBIndex::GetTotalCompressedSize(DatabaseManager& manager)
  {
    MongoDBStatisticsValues values;
    MongoDBStatistics(manager).GetValues(values);
    return static_cast<uint64_t>(values.compressedSize_);
  }


  uint64_t MongoDBIndex::GetTotalUncompressedSize(DatabaseManager& manager)
  {
    MongoDBStatisticsValues values;
    MongoDBStatistics(manager).GetValues(values);
    return static_cast<uint64_t>(values.uncompressedSize_);
  }


  bool MongoDBIndex::IsExistingResource(DatabaseManager& manager,
                                        int64_t internalId)
  {
    return MongoDBResources(manager).IsExistingResource(internalId);
  }


  bool MongoDBIndex::IsProtectedPatient(DatabaseManager& manager,
                                        int64_t internalId)
  {
    return MongoDBPatientRecycling(manager).IsProtectedPatient(internalId);
  }


  void MongoDBIndex::ListAvailableMetadata(std::list<int32_t>& target /*out*/,
                                           DatabaseManager& manager,
                                           int64_t id)
  {
    MongoDBMetadata(manager).ListAvailableMetadata(target, id);
  }


  void MongoDBIndex::ListAvailableAttachments(std::list<int32_t>& target /*out*/,
                                              DatabaseManager& manager,
                                              int64_t id)
  {
    MongoDBAttachments(manager).ListAvailableAttachments(target, id);
  }


  void MongoDBIndex::LogChange(DatabaseManager& manager,
                               int32_t changeType,
                               int64_t resourceId,
                               OrthancPluginResourceType resourceType,
                               const char* date)
  {
    MongoDBChanges(manager).LogChange(changeType, resourceId, resourceType, date);
  }


  void MongoDBIndex::LogExportedResource(DatabaseManager& manager,
                                         OrthancPluginResourceType resourceType,
                                         const char* publicId,
                                         const char* modality,
                                         const char* date,
                                         const char* patientId,
                                         const char* studyInstanceUid,
                                         const char* seriesInstanceUid,
                                         const char* sopInstanceUid)
  {
    MongoDBExportedResources(manager).LogExportedResource(resourceType, publicId, modality, date, patientId,
                                                          studyInstanceUid, seriesInstanceUid, sopInstanceUid);
  }


  bool MongoDBIndex::LookupAttachment(IDatabaseBackendOutput& output,
                                      int64_t& revision /*out*/,
                                      DatabaseManager& manager,
                                      int64_t id,
                                      int32_t contentType)
  {
    return MongoDBAttachments(manager).LookupAttachment(output, revision, id, contentType);
  }


  bool MongoDBIndex::LookupGlobalProperty(std::string& target /*out*/,
                                          DatabaseManager& manager,
                                          const char* serverIdentifier,
                                          int32_t property)
  {
    return MongoDBGlobalProperties(manager).LookupGlobalProperty(target, serverIdentifier, property);
  }


  void MongoDBIndex::LookupIdentifier(std::list<int64_t>& target /*out*/,
                                      DatabaseManager& manager,
                                      OrthancPluginResourceType resourceType,
                                      uint16_t group,
                                      uint16_t element,
                                      OrthancPluginIdentifierConstraint constraint,
                                      const char* value)
  {
    MongoDBMainDicomTags(manager).LookupIdentifier(target, group, element, constraint, value);
  }


  void MongoDBIndex::LookupIdentifierRange(std::list<int64_t>& target /*out*/,
                                           DatabaseManager& manager,
                                           OrthancPluginResourceType resourceType,
                                           uint16_t group,
                                           uint16_t element,
                                           const char* start,
                                           const char* end)
  {
    MongoDBMainDicomTags(manager).LookupIdentifierRange(target, group, element, start, end);
  }


  bool MongoDBIndex::LookupMetadata(std::string& target /*out*/,
                                    int64_t& revision /*out*/,
                                    DatabaseManager& manager,
                                    int64_t id,
                                    int32_t metadataType)
  {
    return MongoDBMetadata(manager).LookupMetadata(target, revision, id, metadataType);
  }


  bool MongoDBIndex::LookupParent(int64_t& parentId /*out*/,
                                  DatabaseManager& manager,
                                  int64_t resourceId)
  {
    return MongoDBResources(manager).LookupParent(parentId, resourceId);
  }


  bool MongoDBIndex::LookupResource(int64_t& id /*out*/,
                                    OrthancPluginResourceType& type /*out*/,
                                    DatabaseManager& manager,
                                    const char* publicId)
  {
    return MongoDBResources(manager).LookupResource(id, type, publicId);
  }


  bool MongoDBIndex::SelectPatientToRecycle(int64_t& internalId /*out*/,
                                            DatabaseManager& manager)
  {
    return MongoDBPatientRecycling(manager).SelectPatientToRecycle(internalId);
  }


  bool MongoDBIndex::SelectPatientToRecycle(int64_t& internalId /*out*/,
                                            DatabaseManager& manager,
                                            int64_t patientIdToAvoid)
  {
    return MongoDBPatientRecycling(manager).SelectPatientToRecycle(internalId, patientIdToAvoid);
  }


  void MongoDBIndex::SetGlobalProperty(DatabaseManager& manager,
                                       const char* serverIdentifier,
                                       int32_t property,
                                       const char* utf8)
  {
    MongoDBGlobalProperties(manager).SetGlobalProperty(serverIdentifier, property, utf8);
  }


  void MongoDBIndex::SetMainDicomTag(DatabaseManager& manager,
                                     int64_t id,
                                     uint16_t group,
                                     uint16_t element,
                                     const char* value)
  {
    MongoDBMainDicomTags(manager).SetMainDicomTag(id, group, element, value);
  }


  void MongoDBIndex::SetIdentifierTag(DatabaseManager& manager,
                                      int64_t id,
                                      uint16_t group,
                                      uint16_t element,
                                      const char* value)
  {
    MongoDBMainDicomTags(manager).SetIdentifierTag(id, group, element, value);
  }


  void MongoDBIndex::SetMetadata(DatabaseManager& manager,
                                 int64_t id,
                                 int32_t metadataType,
                                 const char* value,
                                 int64_t revision)
  {
    MongoDBMetadata(manager).SetMetadata(id, metadataType, value, revision);
  }


  void MongoDBIndex::SetProtectedPatient(DatabaseManager& manager,
                                         int64_t internalId,
                                         bool isProtected)
  {
    MongoDBPatientRecycling(manager).SetProtectedPatient(internalId, isProtected);
  }


  void MongoDBIndex::ClearMainDicomTags(DatabaseManager& manager,
                                        int64_t internalId)
  {
    MongoDBMainDicomTags(manager).ClearMainDicomTags(internalId);
  }


#if ORTHANC_PLUGINS_HAS_INTEGRATED_FIND == 1
  void MongoDBIndex::ExecuteFind(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                 DatabaseManager& manager,
                                 const Orthanc::DatabasePluginMessages::Find_Request& request)
  {
    MongoDBFind(manager).ExecuteFind(response, request);
  }


  void MongoDBIndex::ExecuteCount(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                  DatabaseManager& manager,
                                  const Orthanc::DatabasePluginMessages::Find_Request& request)
  {
    MongoDBFind(manager).ExecuteCount(response, request);
  }
#endif


  void MongoDBIndex::LookupResources(IDatabaseBackendOutput& output,
                                     DatabaseManager& manager,
                                     const DatabaseConstraints& lookup,
                                     OrthancPluginResourceType queryLevel,
                                     const std::set<std::string>& labels,
                                     LabelsConstraint labelsConstraint,
                                     uint32_t limit,
                                     bool requestSomeInstance)
  {
    MongoDBLookup(manager).LookupResources(output, lookup, queryLevel, labels, labelsConstraint,
                                           limit, requestSomeInstance);
  }


  void MongoDBIndex::SetResourcesContent(DatabaseManager& manager,
                                         uint32_t countIdentifierTags,
                                         const OrthancPluginResourcesContentTags* identifierTags,
                                         uint32_t countMainDicomTags,
                                         const OrthancPluginResourcesContentTags* mainDicomTags,
                                         uint32_t countMetadata,
                                         const OrthancPluginResourcesContentMetadata* metadata)
  {
    MongoDBMainDicomTags tags(manager);
    tags.SetIdentifierTags(countIdentifierTags, identifierTags);
    tags.SetMainDicomTags(countMainDicomTags, mainDicomTags);

    MongoDBMetadata(manager).SetMetadata(countMetadata, metadata);
  }


  void MongoDBIndex::GetChildrenMetadata(std::list<std::string>& target,
                                         DatabaseManager& manager,
                                         int64_t resourceId,
                                         int32_t metadata)
  {
    MongoDBResources(manager).GetChildrenMetadata(target, resourceId, metadata);
  }


  void MongoDBIndex::TagMostRecentPatient(DatabaseManager& manager,
                                          int64_t patient)
  {
    MongoDBPatientRecycling(manager).TagMostRecentPatient(patient);
  }


  bool MongoDBIndex::LookupResourceAndParent(int64_t& id,
                                             OrthancPluginResourceType& type,
                                             std::string& parentPublicId,
                                             DatabaseManager& manager,
                                             const char* publicId)
  {
    return MongoDBResources(manager).LookupResourceAndParent(id, type, parentPublicId, publicId);
  }


  void MongoDBIndex::GetAllMetadata(std::map<int32_t, std::string>& result,
                                    DatabaseManager& manager,
                                    int64_t id)
  {
    MongoDBMetadata(manager).GetAllMetadata(result, id);
  }


  void MongoDBIndex::CreateInstance(OrthancPluginCreateInstanceResult& result,
                                    DatabaseManager& manager,
                                    const char* hashPatient,
                                    const char* hashStudy,
                                    const char* hashSeries,
                                    const char* hashInstance)
  {
    MongoDBResources(manager).CreateInstance(result, hashPatient, hashStudy, hashSeries, hashInstance);
  }


  int64_t MongoDBIndex::CreateResource(DatabaseManager& manager,
                                       const char* publicId,
                                       OrthancPluginResourceType type)
  {
    return MongoDBResources(manager).CreateResource(publicId, type);
  }


  int64_t MongoDBIndex::GetLastChangeIndex(DatabaseManager& manager)
  {
    return MongoDBChanges(manager).GetLastChangeIndex();
  }


  uint64_t MongoDBIndex::GetAllResourcesCount(DatabaseManager& manager)
  {
    return MongoDBResources(manager).GetAllResourcesCount();
  }


  uint64_t MongoDBIndex::GetUnprotectedPatientsCount(DatabaseManager& manager)
  {
    return MongoDBPatientRecycling(manager).GetUnprotectedPatientsCount();
  }


  bool MongoDBIndex::GetParentPublicId(std::string& target,
                                       DatabaseManager& manager,
                                       int64_t id)
  {
    MongoDBResources resources(manager);

    int64_t parentId;
    if (resources.LookupParent(parentId, id))
    {
      target = resources.GetPublicId(parentId);
      return true;
    }
    else
    {
      return false;
    }
  }


  void MongoDBIndex::GetChildren(std::list<std::string>& childrenPublicIds,
                                 DatabaseManager& manager,
                                 int64_t id)
  {
    MongoDBResources(manager).GetChildrenPublicId(childrenPublicIds, id);
  }
}
