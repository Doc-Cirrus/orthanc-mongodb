/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017  (Doc Cirrus GmbH)   Ronald Wertlen, Ihor Mozil
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

#include <orthanc/OrthancCppDatabasePlugin.h>

#include <mongocxx/pool.hpp>
#include <mutex>

#include "../Core/MongoDBConnection.h"

namespace OrthancPlugins
{
  class MongoDBBackend : public IDatabaseBackend
  {
  private:
    OrthancPluginContext*  context_;
    std::unique_ptr<MongoDBConnection> connection_;
    mongocxx::pool pool_;
    std::string dbname_;
    std::mutex mutex_;

    int64_t GetNextSequence(mongocxx::database& db, const std::string seqName);

    void CreateIndices();
    void CheckMonoDBMaster();
    void ExecuteSetResourcesContentTags(const std::string& collectionName, uint32_t count, const OrthancPluginResourcesContentTags* tags);
    void ExecuteSetResourcesContentMetadata(const std::string& collectionName, uint32_t count, const OrthancPluginResourcesContentMetadata* meta);

  public:
    MongoDBBackend(OrthancPluginContext* context,
      MongoDBConnection* connection);

    virtual ~MongoDBBackend();

    virtual void Open();

    virtual void Close();

    virtual void AddAttachment(int64_t id, const OrthancPluginAttachment& attachment);

    virtual void AttachChild(int64_t parent, int64_t child);

    virtual void ClearChanges();

    virtual void ClearExportedResources();

    virtual int64_t CreateResource(const char* publicId, OrthancPluginResourceType type);

    virtual void DeleteAttachment(int64_t id, int32_t attachment);

    virtual void DeleteMetadata(int64_t id, int32_t metadataType);

    virtual void DeleteResource(int64_t id);

    virtual void GetAllInternalIds(std::list<int64_t>& target, OrthancPluginResourceType resourceType);

    virtual void GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType);

    virtual void GetAllPublicIds(std::list<std::string>& target, OrthancPluginResourceType resourceType,
                   uint64_t since, uint64_t limit);

    /* Use GetOutput().AnswerChange() */
    virtual void GetChanges(bool& done /*out*/, int64_t since, uint32_t maxResults);

    virtual void GetChildrenInternalId(std::list<int64_t>& target /*out*/, int64_t id);

    virtual void GetChildrenPublicId(std::list<std::string>& target /*out*/, int64_t id);

    /* Use GetOutput().AnswerExportedResource() */
    virtual void GetExportedResources(bool& done /*out*/, int64_t since, uint32_t maxResults);

    /* Use GetOutput().AnswerChange() */
    virtual void GetLastChange();

    /* Use GetOutput().AnswerExportedResource() */
    virtual void GetLastExportedResource();

    /* Use GetOutput().AnswerDicomTag() */
    virtual void GetMainDicomTags(int64_t id);

    virtual std::string GetPublicId(int64_t resourceId);

    virtual uint64_t GetResourceCount(OrthancPluginResourceType resourceType);

    virtual OrthancPluginResourceType GetResourceType(int64_t resourceId);

    virtual uint64_t GetTotalCompressedSize();

    virtual uint64_t GetTotalUncompressedSize();

    virtual bool IsExistingResource(int64_t internalId);

    virtual bool IsProtectedPatient(int64_t internalId);

    virtual void ListAvailableMetadata(std::list<int32_t>& target /*out*/, int64_t id);

    virtual void ListAvailableAttachments(std::list<int32_t>& target /*out*/, int64_t id);

    virtual void LogChange(const OrthancPluginChange& change);

    virtual void LogExportedResource(const OrthancPluginExportedResource& resource);

    /* Use GetOutput().AnswerAttachment() */
    virtual bool LookupAttachment(int64_t id, int32_t contentType);

    virtual bool LookupGlobalProperty(std::string& target /*out*/, int32_t property);

    virtual void LookupIdentifier(std::list<int64_t>& target /*out*/,
      OrthancPluginResourceType resourceType,
      uint16_t group,
      uint16_t element,
      OrthancPluginIdentifierConstraint constraint,
      const char* value);

    virtual void LookupIdentifierRange(std::list<long int>& target, 
      OrthancPluginResourceType resourceType, 
      uint16_t group, 
      uint16_t element, 
      const char* start,
      const char* end);

    virtual bool LookupMetadata(std::string& target /*out*/, int64_t id, int32_t metadataType);

    virtual bool LookupParent(int64_t& parentId /*out*/, int64_t resourceId);

    virtual bool LookupResource(int64_t& id /*out*/, OrthancPluginResourceType& type /*out*/, const char* publicId);

    virtual bool SelectPatientToRecycle(int64_t& internalId /*out*/);

    virtual bool SelectPatientToRecycle(int64_t& internalId /*out*/, int64_t patientIdToAvoid);

    virtual void SetGlobalProperty(int32_t property, const char* value);

    virtual void SetMainDicomTag(int64_t id, uint16_t group, uint16_t element, const char* value);

    virtual void SetIdentifierTag(int64_t id, uint16_t group, uint16_t element, const char* value);

    virtual void SetMetadata(int64_t id, int32_t metadataType, const char* value);

    virtual void SetProtectedPatient(int64_t internalId, bool isProtected);

    virtual void StartTransaction();

    virtual void RollbackTransaction();

    virtual void CommitTransaction();

    virtual uint32_t GetDatabaseVersion();

    /**
    * Upgrade the database to the specified version of the database
    * schema.  The upgrade script is allowed to make calls to
    * OrthancPluginReconstructMainDicomTags().
    **/
    virtual void UpgradeDatabase(uint32_t  targetVersion, OrthancPluginStorageArea* storageArea);

    virtual void ClearMainDicomTags(int64_t internalId);


    virtual void GetChildrenMetadata(std::list<std::string>& target,
                                     int64_t resourceId,
                                     int32_t metadata);

    virtual int64_t GetLastChangeIndex();
    
    virtual void TagMostRecentPatient(int64_t patientId);

    virtual bool LookupResourceAndParent(int64_t& id, OrthancPluginResourceType& type, std::string& parentPublicId, const char* publicId);

    virtual void GetAllMetadata(std::map<int32_t, std::string>& result, int64_t id);

    virtual void LookupResources(const std::vector<OrthancPluginDatabaseConstraint>& lookup,
                                 OrthancPluginResourceType queryLevel,
                                 uint32_t limit,
                                 bool requestSomeInstance);
    virtual void SetResourcesContent(
                                uint32_t countIdentifierTags,
                                const OrthancPluginResourcesContentTags* identifierTags,
                                uint32_t countMainDicomTags,
                                const OrthancPluginResourcesContentTags* mainDicomTags,
                                uint32_t countMetadata,
                                const OrthancPluginResourcesContentMetadata* metadata);

    virtual bool HasCreateInstance() const
    {
      return true;
    }

    virtual void CreateInstance(OrthancPluginCreateInstanceResult& result,
                                const char* hashPatient,
                                const char* hashStudy,
                                const char* hashSeries,
                                const char* hashInstance);

  };
} //namespace OrtahncPlugins









