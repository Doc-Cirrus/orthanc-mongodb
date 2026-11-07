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


#include "DatabaseBackendAdapterV4.h"

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)         // Macro introduced in Orthanc 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 0)

#include "DynamicIndexConnectionsPool.h"
#include "IndexConnectionsPool.h"
#include "MessagesToolbox.h"
#include <Toolbox.h>

#include <OrthancDatabasePlugin.pb.h>  // Include protobuf messages

#include <Logging.h>
#include "../../Resources/Orthanc/Plugins/OrthancPluginException.h"

#include <stdexcept>
#include <list>
#include <string>
#include <cassert>


#define ORTHANC_PLUGINS_DATABASE_CATCH(context) \


namespace OrthancDatabases
{
  static bool isBackendInUse_ = false;  // Only for sanity checks

  // The AuditLogHandler necessitates the plugin to manage the resources associated with the pool of connections
  static std::unique_ptr<BaseIndexConnectionsPool> connectionPool_;

  static Orthanc::DatabasePluginMessages::ResourceType Convert(OrthancPluginResourceType resourceType)
  {
    switch (resourceType)
    {
      case OrthancPluginResourceType_Patient:
        return Orthanc::DatabasePluginMessages::RESOURCE_PATIENT;

      case OrthancPluginResourceType_Study:
        return Orthanc::DatabasePluginMessages::RESOURCE_STUDY;

      case OrthancPluginResourceType_Series:
        return Orthanc::DatabasePluginMessages::RESOURCE_SERIES;

      case OrthancPluginResourceType_Instance:
        return Orthanc::DatabasePluginMessages::RESOURCE_INSTANCE;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  static OrthancPluginResourceType Convert(Orthanc::DatabasePluginMessages::ResourceType resourceType)
  {
    switch (resourceType)
    {
      case Orthanc::DatabasePluginMessages::RESOURCE_PATIENT:
        return OrthancPluginResourceType_Patient;

      case Orthanc::DatabasePluginMessages::RESOURCE_STUDY:
        return OrthancPluginResourceType_Study;

      case Orthanc::DatabasePluginMessages::RESOURCE_SERIES:
        return OrthancPluginResourceType_Series;

      case Orthanc::DatabasePluginMessages::RESOURCE_INSTANCE:
        return OrthancPluginResourceType_Instance;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  class Output : public IDatabaseBackendOutput
  {
  private:
    Orthanc::DatabasePluginMessages::DeleteAttachment::Response*         deleteAttachment_;
    Orthanc::DatabasePluginMessages::DeleteResource::Response*           deleteResource_;
    Orthanc::DatabasePluginMessages::GetChanges::Response*               getChanges_;
    Orthanc::DatabasePluginMessages::GetExportedResources::Response*     getExportedResources_;
    Orthanc::DatabasePluginMessages::GetLastChange::Response*            getLastChange_;
    Orthanc::DatabasePluginMessages::GetLastExportedResource::Response*  getLastExportedResource_;
    Orthanc::DatabasePluginMessages::GetMainDicomTags::Response*         getMainDicomTags_;
    Orthanc::DatabasePluginMessages::LookupAttachment::Response*         lookupAttachment_;
    Orthanc::DatabasePluginMessages::LookupResources::Response*          lookupResources_;

#if ORTHANC_PLUGINS_HAS_CHANGES_EXTENDED == 1
    Orthanc::DatabasePluginMessages::GetChangesExtended::Response*       getChangesExtended_;
#endif

    void Clear()
    {
      deleteAttachment_ = NULL;
      deleteResource_ = NULL;
      getChanges_ = NULL;
      getExportedResources_ = NULL;
      getLastChange_ = NULL;
      getLastExportedResource_ = NULL;
      getMainDicomTags_ = NULL;
      lookupAttachment_ = NULL;
      lookupResources_ = NULL;

#if ORTHANC_PLUGINS_HAS_CHANGES_EXTENDED == 1
      getChangesExtended_ = NULL;
#endif
    }
    
  public:
    explicit Output(Orthanc::DatabasePluginMessages::DeleteAttachment::Response& deleteAttachment)
    {
      Clear();
      deleteAttachment_ = &deleteAttachment;
    }
    
    explicit Output(Orthanc::DatabasePluginMessages::DeleteResource::Response& deleteResource)
    {
      Clear();
      deleteResource_ = &deleteResource;
    }
    
    explicit Output(Orthanc::DatabasePluginMessages::GetChanges::Response& getChanges)
    {
      Clear();
      getChanges_ = &getChanges;
    }

#if ORTHANC_PLUGINS_HAS_CHANGES_EXTENDED == 1
    explicit Output(Orthanc::DatabasePluginMessages::GetChangesExtended::Response& getChangesExtended)
    {
      Clear();
      getChangesExtended_ = &getChangesExtended;
    }
#endif

    explicit Output(Orthanc::DatabasePluginMessages::GetExportedResources::Response& getExportedResources)
    {
      Clear();
      getExportedResources_ = &getExportedResources;
    }
    
    explicit Output(Orthanc::DatabasePluginMessages::GetLastChange::Response& getLastChange)
    {
      Clear();
      getLastChange_ = &getLastChange;
    }
    
    explicit Output(Orthanc::DatabasePluginMessages::GetLastExportedResource::Response& getLastExportedResource)
    {
      Clear();
      getLastExportedResource_ = &getLastExportedResource;
    }
    
    explicit Output(Orthanc::DatabasePluginMessages::GetMainDicomTags::Response& getMainDicomTags)
    {
      Clear();
      getMainDicomTags_ = &getMainDicomTags;
    }
    
    explicit Output(Orthanc::DatabasePluginMessages::LookupAttachment::Response& lookupAttachment)
    {
      Clear();
      lookupAttachment_ = &lookupAttachment;
    }
    
    explicit Output(Orthanc::DatabasePluginMessages::LookupResources::Response& lookupResources)
    {
      Clear();
      lookupResources_ = &lookupResources;
    }
    
    virtual void SignalDeletedAttachment(const std::string& uuid,
                                         int32_t            contentType,
                                         uint64_t           uncompressedSize,
                                         const std::string& uncompressedHash,
                                         int32_t            compressionType,
                                         uint64_t           compressedSize,
                                         const std::string& compressedHash,
                                         const std::string& customData) ORTHANC_OVERRIDE
    {
      Orthanc::DatabasePluginMessages::FileInfo* attachment;

      if (deleteAttachment_ != NULL)
      {
        if (deleteAttachment_->has_deleted_attachment())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
        
        attachment = deleteAttachment_->mutable_deleted_attachment();
      }
      else if (deleteResource_ != NULL)
      {
        attachment = deleteResource_->add_deleted_attachments();
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      
      attachment->set_uuid(uuid);
      attachment->set_content_type(contentType);
      attachment->set_uncompressed_size(uncompressedSize);
      attachment->set_uncompressed_hash(uncompressedHash);
      attachment->set_compression_type(compressionType);
      attachment->set_compressed_size(compressedSize);
      attachment->set_compressed_hash(compressedHash);

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
      attachment->set_custom_data(customData);
#endif
    }

    virtual void SignalDeletedResource(const std::string& publicId,
                                       OrthancPluginResourceType resourceType) ORTHANC_OVERRIDE
    {
      if (deleteResource_ != NULL)
      {
        Orthanc::DatabasePluginMessages::DeleteResource_Response_Resource* resource = deleteResource_->add_deleted_resources();
        resource->set_level(Convert(resourceType));
        resource->set_public_id(publicId);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }

    virtual void SignalRemainingAncestor(const std::string& ancestorId,
                                         OrthancPluginResourceType ancestorType) ORTHANC_OVERRIDE
    {
      if (deleteResource_ != NULL)
      {
        if (deleteResource_->is_remaining_ancestor())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
        else
        {
          deleteResource_->set_is_remaining_ancestor(true);
          deleteResource_->mutable_remaining_ancestor()->set_level(Convert(ancestorType));
          deleteResource_->mutable_remaining_ancestor()->set_public_id(ancestorId);
        }
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }
    
    virtual void AnswerAttachment(const std::string& uuid,
                                  int32_t            contentType,
                                  uint64_t           uncompressedSize,
                                  const std::string& uncompressedHash,
                                  int32_t            compressionType,
                                  uint64_t           compressedSize,
                                  const std::string& compressedHash,
                                  const std::string& customData) ORTHANC_OVERRIDE
    {
      if (lookupAttachment_ != NULL)
      {
        if (lookupAttachment_->found())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }

        lookupAttachment_->set_found(true);
        lookupAttachment_->mutable_attachment()->set_uuid(uuid);
        lookupAttachment_->mutable_attachment()->set_content_type(contentType);
        lookupAttachment_->mutable_attachment()->set_uncompressed_size(uncompressedSize);
        lookupAttachment_->mutable_attachment()->set_uncompressed_hash(uncompressedHash);
        lookupAttachment_->mutable_attachment()->set_compression_type(compressionType);
        lookupAttachment_->mutable_attachment()->set_compressed_size(compressedSize);
        lookupAttachment_->mutable_attachment()->set_compressed_hash(compressedHash);
#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
        lookupAttachment_->mutable_attachment()->set_custom_data(customData);
#endif
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }

    virtual void AnswerChange(int64_t                    seq,
                              int32_t                    changeType,
                              OrthancPluginResourceType  resourceType,
                              const std::string&         publicId,
                              const std::string&         date) ORTHANC_OVERRIDE
    {
      Orthanc::DatabasePluginMessages::ServerIndexChange* change;
      
      if (getChanges_ != NULL)
      {
        change = getChanges_->add_changes();
      }
#if ORTHANC_PLUGINS_HAS_CHANGES_EXTENDED == 1
      else if (getChangesExtended_ != NULL)
      {
        change = getChangesExtended_->add_changes();
      }
#endif
      else if (getLastChange_ != NULL)
      {
        if (getLastChange_->found())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }

        getLastChange_->set_found(true);
        change = getLastChange_->mutable_change();
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }

      change->set_seq(seq);
      change->set_change_type(changeType);
      change->set_resource_type(Convert(resourceType));
      change->set_public_id(publicId);
      change->set_date(date);
    }

    virtual void AnswerDicomTag(uint16_t group,
                                uint16_t element,
                                const std::string& value) ORTHANC_OVERRIDE
    {
      if (getMainDicomTags_ != NULL)
      {
        Orthanc::DatabasePluginMessages::GetMainDicomTags_Response_Tag* tag = getMainDicomTags_->add_tags();
        tag->set_group(group);
        tag->set_element(element);
        tag->set_value(value);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }

    virtual void AnswerExportedResource(int64_t                    seq,
                                        OrthancPluginResourceType  resourceType,
                                        const std::string&         publicId,
                                        const std::string&         modality,
                                        const std::string&         date,
                                        const std::string&         patientId,
                                        const std::string&         studyInstanceUid,
                                        const std::string&         seriesInstanceUid,
                                        const std::string&         sopInstanceUid) ORTHANC_OVERRIDE
    {
      Orthanc::DatabasePluginMessages::ExportedResource* resource;

      if (getExportedResources_ != NULL)
      {
        resource = getExportedResources_->add_resources();
      }
      else if (getLastExportedResource_ != NULL)
      {
        if (getLastExportedResource_->found())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }

        getLastExportedResource_->set_found(true);
        resource = getLastExportedResource_->mutable_resource();
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      
      resource->set_seq(seq);
      resource->set_resource_type(Convert(resourceType));
      resource->set_public_id(publicId);
      resource->set_modality(modality);
      resource->set_date(date);
      resource->set_patient_id(patientId);
      resource->set_study_instance_uid(studyInstanceUid);
      resource->set_series_instance_uid(seriesInstanceUid);
      resource->set_sop_instance_uid(sopInstanceUid);
    }
    
    virtual void AnswerMatchingResource(const std::string& resourceId) ORTHANC_OVERRIDE
    {
      if (lookupResources_ != NULL)
      {
        lookupResources_->add_resources_ids(resourceId);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }
    
    virtual void AnswerMatchingResource(const std::string& resourceId,
                                        const std::string& someInstanceId) ORTHANC_OVERRIDE
    {
      if (lookupResources_ != NULL)
      {
        lookupResources_->add_resources_ids(resourceId);
        lookupResources_->add_instances_ids(someInstanceId);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }
  };
  

  static void ProcessDatabaseOperation(Orthanc::DatabasePluginMessages::DatabaseResponse& response,
                                       const Orthanc::DatabasePluginMessages::DatabaseRequest& request,
                                       BaseIndexConnectionsPool& pool)
  {
    switch (request.operation())
    {
      case Orthanc::DatabasePluginMessages::OPERATION_GET_SYSTEM_INFORMATION:
      {
        BaseIndexConnectionsPool::Accessor accessor(pool);
        response.mutable_get_system_information()->set_database_version(accessor.GetBackend().GetDatabaseVersion(accessor.GetManager()));
        response.mutable_get_system_information()->set_supports_flush_to_disk(false);
        response.mutable_get_system_information()->set_supports_revisions(accessor.GetBackend().HasRevisionsSupport());
        response.mutable_get_system_information()->set_supports_labels(accessor.GetBackend().HasLabelsSupport());

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 3)
        response.mutable_get_system_information()->set_supports_increment_global_property(accessor.GetBackend().HasAtomicIncrementGlobalProperty());
        response.mutable_get_system_information()->set_has_update_and_get_statistics(accessor.GetBackend().HasUpdateAndGetStatistics());
        response.mutable_get_system_information()->set_has_measure_latency(accessor.GetBackend().HasMeasureLatency());
#endif

#if ORTHANC_PLUGINS_HAS_INTEGRATED_FIND == 1
        response.mutable_get_system_information()->set_supports_find(accessor.GetBackend().HasFindSupport());
        response.mutable_get_system_information()->set_has_extended_changes(accessor.GetBackend().HasExtendedChanges());
#endif

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
        response.mutable_get_system_information()->set_has_attachment_custom_data(accessor.GetBackend().HasAttachmentCustomDataSupport());
#endif

#if ORTHANC_PLUGINS_HAS_QUEUES == 1
        response.mutable_get_system_information()->set_supports_queues(accessor.GetBackend().HasQueues());
#endif

#if ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE == 1
        response.mutable_get_system_information()->set_supports_reserve_queue_value(accessor.GetBackend().HasReserveQueueValue());
#endif

#if ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES == 1
        response.mutable_get_system_information()->set_supports_key_value_stores(accessor.GetBackend().HasKeyValueStores());
#endif

        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_OPEN:
      {
        std::list<IdentifierTag> identifierTags;

        if (request.open().identifier_tags().empty())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError,
                                          "No identifier tag was provided by the Orthanc core");
        }

        for (int i = 0; i < request.open().identifier_tags().size(); i++)
        {
          const Orthanc::DatabasePluginMessages::Open_Request_IdentifierTag& tag = request.open().identifier_tags(i);
          identifierTags.push_back(IdentifierTag(MessagesToolbox::Convert(tag.level()),
                                                 Orthanc::DicomTag(tag.group(), tag.element()),
                                                 tag.name()));
        }
          
        pool.OpenConnections(true, identifierTags);
        
        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_CLOSE:
        pool.CloseConnections();
        break;

      case Orthanc::DatabasePluginMessages::OPERATION_FLUSH_TO_DISK:
        // Raise an exception since "set_supports_flush_to_disk(false)"
        ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);

      case Orthanc::DatabasePluginMessages::OPERATION_START_TRANSACTION:
      {
        std::unique_ptr<BaseIndexConnectionsPool::Accessor> transaction(new BaseIndexConnectionsPool::Accessor(pool));

        switch (request.start_transaction().type())
        {
          case Orthanc::DatabasePluginMessages::TRANSACTION_READ_ONLY:
            transaction->GetManager().StartTransaction(TransactionType_ReadOnly);
            break;

          case Orthanc::DatabasePluginMessages::TRANSACTION_READ_WRITE:
            transaction->GetManager().StartTransaction(TransactionType_ReadWrite);
            break;

          default:
            throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
        }

        response.mutable_start_transaction()->set_transaction(reinterpret_cast<intptr_t>(transaction.release()));
        break;
      }
        
      case Orthanc::DatabasePluginMessages::OPERATION_UPGRADE:
      {
        BaseIndexConnectionsPool::Accessor accessor(pool);
        OrthancPluginStorageArea* storageArea = reinterpret_cast<OrthancPluginStorageArea*>(request.upgrade().storage_area());
        accessor.GetBackend().UpgradeDatabase(accessor.GetManager(), request.upgrade().target_version(), storageArea);
        break;
      }
              
      case Orthanc::DatabasePluginMessages::OPERATION_FINALIZE_TRANSACTION:
      {
        BaseIndexConnectionsPool::Accessor* transaction = reinterpret_cast<BaseIndexConnectionsPool::Accessor*>(request.finalize_transaction().transaction());
        
        if (transaction == NULL)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
        }
        else
        {
          delete transaction;
        }
        
        break;
      }

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 3)
      case Orthanc::DatabasePluginMessages::OPERATION_MEASURE_LATENCY:
      {
        BaseIndexConnectionsPool::Accessor accessor(pool);
        response.mutable_measure_latency()->set_latency_us(accessor.GetBackend().MeasureLatency(accessor.GetManager()));
        break;
      }
#endif

      default:
        LOG(ERROR) << "Not implemented database operation from protobuf: " << request.operation();
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  static void ApplyLookupResources(Orthanc::DatabasePluginMessages::LookupResources_Response& response,
                                   const Orthanc::DatabasePluginMessages::LookupResources_Request& request,
                                   IndexBackend& backend,
                                   DatabaseManager& manager)
  {
    size_t countValues = 0;

    for (int i = 0; i < request.lookup().size(); i++)
    {
      const Orthanc::DatabasePluginMessages::DatabaseConstraint& constraint = request.lookup(i);
      countValues += constraint.values().size();
    }

    std::vector<size_t> valuesIndex;
    valuesIndex.resize(request.lookup().size());

    std::vector<const char*> values;
    values.reserve(countValues);

    for (int i = 0; i < request.lookup().size(); i++)
    {
      valuesIndex[i] = values.size();

      const Orthanc::DatabasePluginMessages::DatabaseConstraint& constraint = request.lookup(i);

      for (int j = 0; j < constraint.values().size(); j++)
      {
        values.push_back(constraint.values(j).c_str());
      }
    }

    assert(values.size() == countValues);

    DatabaseConstraints lookup;

    for (int i = 0; i < request.lookup().size(); i++)
    {
      const Orthanc::DatabasePluginMessages::DatabaseConstraint& constraint = request.lookup(i);

      if (constraint.tag_group() > 0xffffu ||
          constraint.tag_element() > 0xffffu)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
          
      OrthancPluginDatabaseConstraint c;
      c.level = Convert(constraint.level());
      c.tagGroup = constraint.tag_group();
      c.tagElement = constraint.tag_element();
      c.isIdentifierTag = (constraint.is_identifier_tag() ? 1 : 0);
      c.isCaseSensitive = (constraint.is_case_sensitive() ? 1 : 0);
      c.isMandatory = (constraint.is_mandatory() ? 1 : 0);

      switch (constraint.type())
      {
        case Orthanc::DatabasePluginMessages::CONSTRAINT_EQUAL:
          c.type = OrthancPluginConstraintType_Equal;
          break;
              
        case Orthanc::DatabasePluginMessages::CONSTRAINT_SMALLER_OR_EQUAL:
          c.type = OrthancPluginConstraintType_SmallerOrEqual;
          break;
              
        case Orthanc::DatabasePluginMessages::CONSTRAINT_GREATER_OR_EQUAL:
          c.type = OrthancPluginConstraintType_GreaterOrEqual;
          break;
              
        case Orthanc::DatabasePluginMessages::CONSTRAINT_WILDCARD:
          c.type = OrthancPluginConstraintType_Wildcard;
          break;
              
        case Orthanc::DatabasePluginMessages::CONSTRAINT_LIST:
          c.type = OrthancPluginConstraintType_List;
          break;
              
        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
          
      c.valuesCount = constraint.values().size();

      if (c.valuesCount == 0)
      {
        c.values = NULL;
      }
      else
      {
        c.values = &values[valuesIndex[i]];
      }

      lookup.AddConstraint(new DatabaseConstraint(c));
    }

    assert(values.size() == countValues);

    std::set<std::string> labels;

    for (int i = 0; i < request.labels().size(); i++)
    {
      labels.insert(request.labels(i));
    }

    LabelsConstraint labelsConstraint;
    switch (request.labels_constraint())
    {
      case Orthanc::DatabasePluginMessages::LABELS_CONSTRAINT_ALL:
        labelsConstraint = LabelsConstraint_All;
        break;
            
      case Orthanc::DatabasePluginMessages::LABELS_CONSTRAINT_ANY:
        labelsConstraint = LabelsConstraint_Any;
        break;
            
      case Orthanc::DatabasePluginMessages::LABELS_CONSTRAINT_NONE:
        labelsConstraint = LabelsConstraint_None;
        break;
            
      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    Output output(response);
    backend.LookupResources(output, manager, lookup, Convert(request.query_level()),
                            labels, labelsConstraint, request.limit(), request.retrieve_instances_ids());
  }

  
  static void ProcessTransactionOperation(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                                          const Orthanc::DatabasePluginMessages::TransactionRequest& request,
                                          IndexBackend& backend,
                                          DatabaseManager& manager)
  {
    switch (request.operation())
    {
      case Orthanc::DatabasePluginMessages::OPERATION_ROLLBACK:
        manager.RollbackTransaction();
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_COMMIT:
        manager.CommitTransaction();
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_ADD_ATTACHMENT:
      {
        OrthancPluginAttachment attachment;
        attachment.uuid = request.add_attachment().attachment().uuid().c_str();
        attachment.contentType = request.add_attachment().attachment().content_type();
        attachment.uncompressedSize = request.add_attachment().attachment().uncompressed_size();
        attachment.uncompressedHash = request.add_attachment().attachment().uncompressed_hash().c_str();
        attachment.compressionType = request.add_attachment().attachment().compression_type();
        attachment.compressedSize = request.add_attachment().attachment().compressed_size();
        attachment.compressedHash = request.add_attachment().attachment().compressed_hash().c_str();

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
        backend.AddAttachment(manager, request.add_attachment().id(), attachment, request.add_attachment().revision(),
                              request.add_attachment().attachment().custom_data());
#else
        backend.AddAttachment(manager, request.add_attachment().id(), attachment, request.add_attachment().revision());
#endif

        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_CLEAR_CHANGES:
        backend.ClearChanges(manager);
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_CLEAR_EXPORTED_RESOURCES:
        backend.ClearExportedResources(manager);
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_DELETE_ATTACHMENT:
      {
        Output output(*response.mutable_delete_attachment());
        backend.DeleteAttachment(output, manager, request.delete_attachment().id(), request.delete_attachment().type());
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_DELETE_METADATA:
        backend.DeleteMetadata(manager, request.delete_metadata().id(), request.delete_metadata().type());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_DELETE_RESOURCE:
      {
        response.mutable_delete_resource()->set_is_remaining_ancestor(false);

        Output output(*response.mutable_delete_resource());
        backend.DeleteResource(output, manager, request.delete_resource().id());
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_ALL_METADATA:
      {
        typedef std::map<int32_t, std::string>  Values;

        Values values;
        backend.GetAllMetadata(values, manager, request.get_all_metadata().id());

        response.mutable_get_all_metadata()->mutable_metadata()->Reserve(values.size());
        for (Values::const_iterator it = values.begin(); it != values.end(); ++it)
        {
          Orthanc::DatabasePluginMessages::GetAllMetadata_Response_Metadata* metadata =
            response.mutable_get_all_metadata()->add_metadata();
          metadata->set_type(it->first);
          metadata->set_value(it->second);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_ALL_PUBLIC_IDS:
      {
        std::list<std::string>  values;
        backend.GetAllPublicIds(values, manager, Convert(request.get_all_public_ids().resource_type()));

        response.mutable_get_all_public_ids()->mutable_ids()->Reserve(values.size());
        for (std::list<std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
        {
          response.mutable_get_all_public_ids()->add_ids(*it);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_ALL_PUBLIC_IDS_WITH_LIMITS:
      {
        std::list<std::string>  values;
        backend.GetAllPublicIds(values, manager, Convert(request.get_all_public_ids_with_limits().resource_type()),
                                request.get_all_public_ids_with_limits().since(),
                                request.get_all_public_ids_with_limits().limit());

        response.mutable_get_all_public_ids_with_limits()->mutable_ids()->Reserve(values.size());
        for (std::list<std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
        {
          response.mutable_get_all_public_ids_with_limits()->add_ids(*it);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_CHANGES:
      {
        Output output(*response.mutable_get_changes());

        bool done;
        backend.GetChanges(output, done, manager, request.get_changes().since(), request.get_changes().limit());

        response.mutable_get_changes()->set_done(done);
        break;
      }

#if ORTHANC_PLUGINS_HAS_CHANGES_EXTENDED == 1
      case Orthanc::DatabasePluginMessages::OPERATION_GET_CHANGES_EXTENDED:
      {
        Output output(*response.mutable_get_changes_extended());

        bool done;
        std::set<uint32_t> changeTypes;
        for (int i = 0; i < request.get_changes_extended().change_type_size(); ++i)
        {
          changeTypes.insert(request.get_changes_extended().change_type(i));
        }

        backend.GetChangesExtended(output, done, manager, request.get_changes_extended().since(), request.get_changes_extended().to(), changeTypes, request.get_changes_extended().limit());

        response.mutable_get_changes_extended()->set_done(done);
        break;
      }
#endif

      case Orthanc::DatabasePluginMessages::OPERATION_GET_CHILDREN_INTERNAL_ID:
      {
        std::list<int64_t>  values;
        backend.GetChildrenInternalId(values, manager, request.get_children_internal_id().id());

        response.mutable_get_children_internal_id()->mutable_ids()->Reserve(values.size());
        for (std::list<int64_t>::const_iterator it = values.begin(); it != values.end(); ++it)
        {
          response.mutable_get_children_internal_id()->add_ids(*it);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_CHILDREN_PUBLIC_ID:
      {
        std::list<std::string>  values;
        backend.GetChildrenPublicId(values, manager, request.get_children_public_id().id());

        response.mutable_get_children_public_id()->mutable_ids()->Reserve(values.size());
        for (std::list<std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
        {
          response.mutable_get_children_public_id()->add_ids(*it);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_EXPORTED_RESOURCES:
      {
        Output output(*response.mutable_get_exported_resources());

        bool done;
        backend.GetExportedResources(output, done, manager, request.get_exported_resources().since(),
                                     request.get_exported_resources().limit());

        response.mutable_get_exported_resources()->set_done(done);
        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_GET_LAST_CHANGE:
      {
        response.mutable_get_last_change()->set_found(false);

        Output output(*response.mutable_get_last_change());
        backend.GetLastChange(output, manager);
        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_GET_LAST_EXPORTED_RESOURCE:
      {
        response.mutable_get_last_exported_resource()->set_found(false);

        Output output(*response.mutable_get_last_exported_resource());
        backend.GetLastExportedResource(output, manager);
        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_GET_MAIN_DICOM_TAGS:
      {
        Output output(*response.mutable_get_main_dicom_tags());
        backend.GetMainDicomTags(output, manager, request.get_main_dicom_tags().id());
        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_GET_PUBLIC_ID:
      {
        const std::string id = backend.GetPublicId(manager, request.get_public_id().id());
        response.mutable_get_public_id()->set_id(id);
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_RESOURCES_COUNT:
      {
        OrthancPluginResourceType type = Convert(request.get_resources_count().type());
        uint64_t count = backend.GetResourcesCount(manager, type);
        response.mutable_get_resources_count()->set_count(count);
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_RESOURCE_TYPE:
      {
        OrthancPluginResourceType type = backend.GetResourceType(manager, request.get_resource_type().id());
        response.mutable_get_resource_type()->set_type(Convert(type));
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_TOTAL_COMPRESSED_SIZE:
        response.mutable_get_total_compressed_size()->set_size(backend.GetTotalCompressedSize(manager));
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_TOTAL_UNCOMPRESSED_SIZE:
        response.mutable_get_total_uncompressed_size()->set_size(backend.GetTotalUncompressedSize(manager));
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_IS_PROTECTED_PATIENT:
      {
        bool isProtected = backend.IsProtectedPatient(manager, request.is_protected_patient().patient_id());
        response.mutable_is_protected_patient()->set_protected_patient(isProtected);
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_LIST_AVAILABLE_ATTACHMENTS:
      {
        std::list<int32_t>  values;
        backend.ListAvailableAttachments(values, manager, request.list_available_attachments().id());

        response.mutable_list_available_attachments()->mutable_attachments()->Reserve(values.size());
        for (std::list<int32_t>::const_iterator it = values.begin(); it != values.end(); ++it)
        {
          response.mutable_list_available_attachments()->add_attachments(*it);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_LOG_CHANGE:
        backend.LogChange(manager, request.log_change().change_type(),
                          request.log_change().resource_id(),
                          Convert(request.log_change().resource_type()),
                          request.log_change().date().c_str());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_LOG_EXPORTED_RESOURCE:
        backend.LogExportedResource(manager,
                                    Convert(request.log_exported_resource().resource_type()),
                                    request.log_exported_resource().public_id().c_str(),
                                    request.log_exported_resource().modality().c_str(),
                                    request.log_exported_resource().date().c_str(),
                                    request.log_exported_resource().patient_id().c_str(),
                                    request.log_exported_resource().study_instance_uid().c_str(),
                                    request.log_exported_resource().series_instance_uid().c_str(),
                                    request.log_exported_resource().sop_instance_uid().c_str());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_LOOKUP_ATTACHMENT:
      {
        Output output(*response.mutable_lookup_attachment());
        
        int64_t revision = -1;
        backend.LookupAttachment(output, revision, manager, request.lookup_attachment().id(), request.lookup_attachment().content_type());

        if (response.lookup_attachment().found())
        {
          response.mutable_lookup_attachment()->set_revision(revision);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_LOOKUP_GLOBAL_PROPERTY:
      {
        std::string value;
        if (backend.LookupGlobalProperty(value, manager, request.lookup_global_property().server_id().c_str(),
                                         request.lookup_global_property().property()))
        {
          response.mutable_lookup_global_property()->set_found(true);
          response.mutable_lookup_global_property()->set_value(value);
        }
        else
        {
          response.mutable_lookup_global_property()->set_found(false);
        }
        
        break;
      }

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 3)
      case Orthanc::DatabasePluginMessages::OPERATION_UPDATE_AND_GET_STATISTICS:
      {
        int64_t patientsCount, studiesCount, seriesCount, instancesCount, compressedSize, uncompressedSize;
        backend.UpdateAndGetStatistics(manager, patientsCount, studiesCount, seriesCount, instancesCount, compressedSize, uncompressedSize);

        response.mutable_update_and_get_statistics()->set_patients_count(patientsCount);
        response.mutable_update_and_get_statistics()->set_studies_count(studiesCount);
        response.mutable_update_and_get_statistics()->set_series_count(seriesCount);
        response.mutable_update_and_get_statistics()->set_instances_count(instancesCount);
        response.mutable_update_and_get_statistics()->set_total_compressed_size(compressedSize);
        response.mutable_update_and_get_statistics()->set_total_uncompressed_size(uncompressedSize);
        break;
      }
#endif

#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 3)
      case Orthanc::DatabasePluginMessages::OPERATION_INCREMENT_GLOBAL_PROPERTY:
      {
        int64_t value = backend.IncrementGlobalProperty(manager, request.increment_global_property().server_id().c_str(),
                                                        request.increment_global_property().property(),
                                                        request.increment_global_property().increment());
        response.mutable_increment_global_property()->set_new_value(value);
        break;
      }
#endif

      case Orthanc::DatabasePluginMessages::OPERATION_LOOKUP_METADATA:
      {
        std::string value;
        int64_t revision = -1;
        if (backend.LookupMetadata(value, revision, manager, request.lookup_metadata().id(), request.lookup_metadata().metadata_type()))
        {
          response.mutable_lookup_metadata()->set_found(true);
          response.mutable_lookup_metadata()->set_value(value);
          response.mutable_lookup_metadata()->set_revision(revision);
        }
        else
        {
          response.mutable_lookup_metadata()->set_found(false);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_LOOKUP_PARENT:
      {
        int64_t parent = -1;
        if (backend.LookupParent(parent, manager, request.lookup_parent().id()))
        {
          response.mutable_lookup_parent()->set_found(true);
          response.mutable_lookup_parent()->set_parent(parent);
        }
        else
        {
          response.mutable_lookup_parent()->set_found(false);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_LOOKUP_RESOURCE:
      {
        int64_t internalId = -1;
        OrthancPluginResourceType type;
        if (backend.LookupResource(internalId, type, manager, request.lookup_resource().public_id().c_str()))
        {
          response.mutable_lookup_resource()->set_found(true);
          response.mutable_lookup_resource()->set_internal_id(internalId);
          response.mutable_lookup_resource()->set_type(Convert(type));
        }
        else
        {
          response.mutable_lookup_resource()->set_found(false);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_SELECT_PATIENT_TO_RECYCLE:
      {
        int64_t patientId = -1;
        if (backend.SelectPatientToRecycle(patientId, manager))
        {
          response.mutable_select_patient_to_recycle()->set_found(true);
          response.mutable_select_patient_to_recycle()->set_patient_id(patientId);
        }
        else
        {
          response.mutable_select_patient_to_recycle()->set_found(false);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_SELECT_PATIENT_TO_RECYCLE_WITH_AVOID:
      {
        int64_t patientId = -1;
        if (backend.SelectPatientToRecycle(patientId, manager, request.select_patient_to_recycle_with_avoid().patient_id_to_avoid()))
        {
          response.mutable_select_patient_to_recycle_with_avoid()->set_found(true);
          response.mutable_select_patient_to_recycle_with_avoid()->set_patient_id(patientId);
        }
        else
        {
          response.mutable_select_patient_to_recycle_with_avoid()->set_found(false);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_SET_GLOBAL_PROPERTY:
        backend.SetGlobalProperty(manager, request.set_global_property().server_id().c_str(),
                                  request.set_global_property().property(),
                                  request.set_global_property().value().c_str());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_CLEAR_MAIN_DICOM_TAGS:
        backend.ClearMainDicomTags(manager, request.clear_main_dicom_tags().id());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_SET_METADATA:
        backend.SetMetadata(manager, request.set_metadata().id(),
                            request.set_metadata().metadata_type(),
                            request.set_metadata().value().c_str(),
                            request.set_metadata().revision());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_SET_PROTECTED_PATIENT:
        backend.SetProtectedPatient(manager, request.set_protected_patient().patient_id(),
                                    request.set_protected_patient().protected_patient());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_IS_DISK_SIZE_ABOVE:
      {
        bool above = (backend.GetTotalCompressedSize(manager) >= request.is_disk_size_above().threshold());
        response.mutable_is_disk_size_above()->set_result(above);
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_LOOKUP_RESOURCES:
        ApplyLookupResources(*response.mutable_lookup_resources(), request.lookup_resources(), backend, manager);
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_CREATE_INSTANCE:
      {
        const char* hashPatient = request.create_instance().patient().c_str();
        const char* hashStudy = request.create_instance().study().c_str();
        const char* hashSeries = request.create_instance().series().c_str();
        const char* hashInstance = request.create_instance().instance().c_str();
        
        OrthancPluginCreateInstanceResult result;
        
        if (backend.HasCreateInstance())
        {
          backend.CreateInstance(result, manager, hashPatient, hashStudy, hashSeries, hashInstance);
        }
        else
        {
          backend.CreateInstanceGeneric(result, manager, hashPatient, hashStudy, hashSeries, hashInstance);
        }

        response.mutable_create_instance()->set_is_new_instance(result.isNewInstance);
        response.mutable_create_instance()->set_instance_id(result.instanceId);

        if (result.isNewInstance)
        {
          response.mutable_create_instance()->set_is_new_patient(result.isNewPatient);
          response.mutable_create_instance()->set_is_new_study(result.isNewStudy);
          response.mutable_create_instance()->set_is_new_series(result.isNewSeries);
          response.mutable_create_instance()->set_patient_id(result.patientId);
          response.mutable_create_instance()->set_study_id(result.studyId);
          response.mutable_create_instance()->set_series_id(result.seriesId);
        }
        
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_SET_RESOURCES_CONTENT:
      {
        std::vector<OrthancPluginResourcesContentTags> identifierTags;
        std::vector<OrthancPluginResourcesContentTags> mainDicomTags;

        identifierTags.reserve(request.set_resources_content().tags().size());
        mainDicomTags.reserve(request.set_resources_content().tags().size());
        
        for (int i = 0; i < request.set_resources_content().tags().size(); i++)
        {
          if (request.set_resources_content().tags(i).group() > 0xffffu ||
              request.set_resources_content().tags(i).element() > 0xffffu)
          {
            throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
          }
          
          OrthancPluginResourcesContentTags tag;
          tag.resource = request.set_resources_content().tags(i).resource_id();
          tag.group = request.set_resources_content().tags(i).group();
          tag.element = request.set_resources_content().tags(i).element();
          tag.value = request.set_resources_content().tags(i).value().c_str();

          if (request.set_resources_content().tags(i).is_identifier())
          {
            identifierTags.push_back(tag);
          }
          else
          {
            mainDicomTags.push_back(tag);
          }
        }
        
        std::vector<OrthancPluginResourcesContentMetadata> metadata;
        metadata.reserve(request.set_resources_content().metadata().size());
        
        for (int i = 0; i < request.set_resources_content().metadata().size(); i++)
        {
          OrthancPluginResourcesContentMetadata item;
          item.resource = request.set_resources_content().metadata(i).resource_id();
          item.metadata = request.set_resources_content().metadata(i).metadata();
          item.value = request.set_resources_content().metadata(i).value().c_str();
          metadata.push_back(item);
        }

        backend.SetResourcesContent(manager,
                                    identifierTags.size(), (identifierTags.empty() ? NULL : &identifierTags[0]),
                                    mainDicomTags.size(), (mainDicomTags.empty() ? NULL : &mainDicomTags[0]),
                                    metadata.size(), (metadata.empty() ? NULL : &metadata[0]));
        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_CHILDREN_METADATA:
      {
        std::list<std::string> values;
        backend.GetChildrenMetadata(values, manager, request.get_children_metadata().id(), request.get_children_metadata().metadata());

        response.mutable_get_children_metadata()->mutable_values()->Reserve(values.size());
        for (std::list<std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
        {
          response.mutable_get_children_metadata()->add_values(*it);
        }

        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_GET_LAST_CHANGE_INDEX:
        response.mutable_get_last_change_index()->set_result(backend.GetLastChangeIndex(manager));
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_LOOKUP_RESOURCE_AND_PARENT:
      {
        int64_t id;
        OrthancPluginResourceType type;
        std::string parent;
        
        if (backend.LookupResourceAndParent(id, type, parent, manager, request.lookup_resource_and_parent().public_id().c_str()))
        {
          response.mutable_lookup_resource_and_parent()->set_found(true);
          response.mutable_lookup_resource_and_parent()->set_id(id);
          response.mutable_lookup_resource_and_parent()->set_type(Convert(type));

          switch (type)
          {
            case OrthancPluginResourceType_Study:
            case OrthancPluginResourceType_Series:
            case OrthancPluginResourceType_Instance:
              if (parent.empty())
              {
                ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
              }
              else
              {
                response.mutable_lookup_resource_and_parent()->set_parent_public_id(parent);
              }
              break;

            case OrthancPluginResourceType_Patient:
              if (!parent.empty())
              {
                ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
              }
              break;

            default:
              throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
          }
        }
        else
        {
          response.mutable_lookup_resource_and_parent()->set_found(false);
        }

        break;
      }
      
      case Orthanc::DatabasePluginMessages::OPERATION_ADD_LABEL:
        backend.AddLabel(manager, request.add_label().id(), request.add_label().label());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_REMOVE_LABEL:
        backend.RemoveLabel(manager, request.remove_label().id(), request.remove_label().label());
        break;
      
      case Orthanc::DatabasePluginMessages::OPERATION_LIST_LABELS:
      {
        std::list<std::string>  labels;

        if (request.list_labels().single_resource())
        {
          backend.ListLabels(labels, manager, request.list_labels().id());
        }
        else
        {
          backend.ListAllLabels(labels, manager);
        }

        response.mutable_list_available_attachments()->mutable_attachments()->Reserve(labels.size());
        for (std::list<std::string>::const_iterator it = labels.begin(); it != labels.end(); ++it)
        {
          response.mutable_list_labels()->add_labels(*it);
        }
        
        break;
      }
      
#if ORTHANC_PLUGINS_HAS_INTEGRATED_FIND == 1
      case Orthanc::DatabasePluginMessages::OPERATION_FIND:
        backend.ExecuteFind(response, manager, request.find());
        break;

      case Orthanc::DatabasePluginMessages::OPERATION_COUNT_RESOURCES:
        backend.ExecuteCount(response, manager, request.find());
        break;
#endif

#if ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES == 1
      case Orthanc::DatabasePluginMessages::OPERATION_STORE_KEY_VALUE:
        backend.StoreKeyValue(manager, 
                              request.store_key_value().store_id(),
                              request.store_key_value().key(),
                              request.store_key_value().value());
        break;

      case Orthanc::DatabasePluginMessages::OPERATION_GET_KEY_VALUE:
      {
        std::string value;
        bool found = backend.GetKeyValue(value, manager,
                                         request.get_key_value().store_id(),
                                         request.get_key_value().key());
        response.mutable_get_key_value()->set_found(found);

        if (found)
        {
          response.mutable_get_key_value()->set_value(value);
        }
        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_DELETE_KEY_VALUE:
        backend.DeleteKeyValue(manager, 
                               request.delete_key_value().store_id(),
                               request.delete_key_value().key());
        break;

      case Orthanc::DatabasePluginMessages::OPERATION_LIST_KEY_VALUES:
        backend.ListKeysValues(response, manager, request.list_keys_values());
        break;

#endif

#if ORTHANC_PLUGINS_HAS_QUEUES == 1
      case Orthanc::DatabasePluginMessages::OPERATION_ENQUEUE_VALUE:
        backend.EnqueueValue(manager,
                             request.enqueue_value().queue_id(),
                             request.enqueue_value().value());
        break;

      case Orthanc::DatabasePluginMessages::OPERATION_DEQUEUE_VALUE:
      {
        std::string value;
        bool found = backend.DequeueValue(value, manager,
                                          request.dequeue_value().queue_id(),
                                          request.dequeue_value().origin() == Orthanc::DatabasePluginMessages::QUEUE_ORIGIN_FRONT);
        response.mutable_dequeue_value()->set_found(found);
        
        if (found)
        {
          response.mutable_dequeue_value()->set_value(value);
        }

        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_GET_QUEUE_SIZE:
      {
        uint64_t size = backend.GetQueueSize(manager,
                                             request.get_queue_size().queue_id());
        response.mutable_get_queue_size()->set_size(size);
        break;
      }

#endif

#if ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE == 1
      case Orthanc::DatabasePluginMessages::OPERATION_RESERVE_QUEUE_VALUE:
      {
        std::string value;
        uint64_t valueId;
        bool found = backend.ReserveQueueValue(value, valueId, manager,
                                               request.reserve_queue_value().queue_id(),
                                               request.reserve_queue_value().origin() == Orthanc::DatabasePluginMessages::QUEUE_ORIGIN_FRONT,
                                               request.reserve_queue_value().release_timeout());
        response.mutable_reserve_queue_value()->set_found(found);
        
        if (found)
        {
          response.mutable_reserve_queue_value()->set_value(value);
          response.mutable_reserve_queue_value()->set_value_id(valueId);
        }

        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_ACKNOWLEDGE_QUEUE_VALUE:
      {
        backend.AcknowledgeQueueValue(manager,
                                      request.acknowledge_queue_value().queue_id(),
                                      request.acknowledge_queue_value().value_id());

        break;
      }

#endif

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
      case Orthanc::DatabasePluginMessages::OPERATION_GET_ATTACHMENT_CUSTOM_DATA:
      {
        std::string customData;
        backend.GetAttachmentCustomData(customData, manager, request.get_attachment_custom_data().uuid());
        response.mutable_get_attachment_custom_data()->set_custom_data(customData);
        break;
      }

      case Orthanc::DatabasePluginMessages::OPERATION_SET_ATTACHMENT_CUSTOM_DATA:
        backend.SetAttachmentCustomData(manager,
                                        request.set_attachment_custom_data().uuid(),
                                        request.set_attachment_custom_data().custom_data());
        break;
#endif

      default:
        LOG(ERROR) << "Not implemented transaction operation from protobuf: " << request.operation();
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }

  
  static OrthancPluginErrorCode CallBackend(OrthancPluginMemoryBuffer64* serializedResponse,
                                            void* rawPool,
                                            const void* requestData,
                                            uint64_t requestSize)
  {
    if (rawPool == NULL ||
        connectionPool_.get() == NULL ||
        rawPool != connectionPool_.get())
    {
      LOG(ERROR) << "Internal error: Incorrect state for the pool of connections";
      return OrthancPluginErrorCode_InternalError;
    }

    if (!isBackendInUse_)
    {
      LOG(ERROR) << "More than one index backend was registered, internal error";
      return OrthancPluginErrorCode_InternalError;
    }

    Orthanc::DatabasePluginMessages::Request request;
    if (!request.ParseFromArray(requestData, requestSize))
    {
      LOG(ERROR) << "Cannot parse message from the Orthanc core using protobuf";
      return OrthancPluginErrorCode_InternalError;
    }

    try
    {
      Orthanc::DatabasePluginMessages::Response response;
      
      switch (request.type())
      {
        case Orthanc::DatabasePluginMessages::REQUEST_DATABASE:
          ProcessDatabaseOperation(*response.mutable_database_response(), request.database_request(), *connectionPool_);
          break;
          
        case Orthanc::DatabasePluginMessages::REQUEST_TRANSACTION:
        {
          const BaseIndexConnectionsPool::Accessor& transaction = *reinterpret_cast<const BaseIndexConnectionsPool::Accessor*>(request.transaction_request().transaction());
          ProcessTransactionOperation(*response.mutable_transaction_response(), request.transaction_request(),
                                      transaction.GetBackend(), transaction.GetManager());
          break;
        }
          
        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Not implemented request type from protobuf: " +
                                          boost::lexical_cast<std::string>(request.type()));
      }

      std::string s;
      if (!response.SerializeToString(&s))
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError, "Cannot serialize to protobuf");
      }

      if (OrthancPluginCreateMemoryBuffer64(connectionPool_->GetContext(), serializedResponse, s.size()) != OrthancPluginErrorCode_Success)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NotEnoughMemory, "Cannot allocate a memory buffer");
      }

      if (!s.empty())
      {
        assert(serializedResponse->size == s.size());
        memcpy(serializedResponse->data, s.c_str(), s.size());
      }

      return OrthancPluginErrorCode_Success;
    }
    catch (::Orthanc::OrthancException& e)
    {
      if (e.GetErrorCode() == ::Orthanc::ErrorCode_DatabaseCannotSerialize)
      {
        LOG(WARNING) << "An SQL transaction failed and will likely be retried: " << e.GetDetails();
      }
      else
      {
        LOG(ERROR) << "Exception in database back-end: " << e.What();
      }
      return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());
    }
    catch (::std::runtime_error& e)
    {
      LOG(ERROR) << "Exception in database back-end: " << e.what();
      return OrthancPluginErrorCode_DatabasePlugin;
    }
    catch (...)
    {
      LOG(ERROR) << "Native exception";
      return OrthancPluginErrorCode_DatabasePlugin;
    }
  }

  static void FinalizeBackend(void* rawPool)
  {
    if (rawPool == NULL ||
        connectionPool_.get() == NULL ||
        connectionPool_.get() != rawPool)
    {
      LOG(ERROR) << "Internal error: Incorrect state for the pool of connections";
    }

    if (isBackendInUse_)
    {
      isBackendInUse_ = false;
    }
    else
    {
      LOG(ERROR) << "More than one index backend was registered, internal error";
    }

    connectionPool_.reset(NULL);
  }


  OrthancPluginErrorCode AuditLogHandler(const char*               sourcePlugin,
                                         const char*               userId,
                                         OrthancPluginResourceType resourceType,
                                         const char*               resourceId,
                                         const char*               action,
                                         const void*               logData,
                                         uint32_t                  logDataSize)
  {
    if (!isBackendInUse_ ||
        connectionPool_.get() == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }

#if ORTHANC_PLUGINS_HAS_AUDIT_LOGS == 1
    {
      BaseIndexConnectionsPool::Accessor accessor(*connectionPool_);
      accessor.GetBackend().RecordAuditLog(accessor.GetManager(),
                                           sourcePlugin,
                                           userId,
                                           resourceType,
                                           resourceId,
                                           action,
                                           logData,
                                           logDataSize);
    }
#endif

    return OrthancPluginErrorCode_Success;                                     
  }
  
  void GetAuditLogs(OrthancPluginRestOutput* output,
                    const char* /*url*/,
                    const OrthancPluginHttpRequest* request)
  {
    OrthancPluginContext* context = OrthancPlugins::GetGlobalContext();

    if (request->method != OrthancPluginHttpMethod_Get)
    {
      OrthancPluginSendMethodNotAllowed(context, output, "GET");
    }

    OrthancPlugins::GetArguments getArguments;
    OrthancPlugins::GetGetArguments(getArguments, request);

    std::string userIdFilter;
    std::string resourceIdFilter;
    std::string actionFilter;

    uint64_t since = 0;
    uint64_t limit = 0;
    std::string fromTsIsoFormat;
    std::string toTsIsoFormat;
    bool logDataInJson = false;

    if (getArguments.find("user-id") != getArguments.end())
    {
      userIdFilter = getArguments["user-id"];
    }

    if (getArguments.find("resource-id") != getArguments.end())
    {
      resourceIdFilter = getArguments["resource-id"];
    }

    if (getArguments.find("action") != getArguments.end())
    {
      actionFilter = getArguments["action"];
    }

    if (getArguments.find("limit") != getArguments.end())
    {
      limit = boost::lexical_cast<uint64_t>(getArguments["limit"]);
    }

    if (getArguments.find("since") != getArguments.end())
    {
      since = boost::lexical_cast<uint64_t>(getArguments["since"]);
    }

    if (getArguments.find("from-timestamp") != getArguments.end())
    {
      fromTsIsoFormat = getArguments["from-timestamp"];
    }

    if (getArguments.find("to-timestamp") != getArguments.end())
    {
      toTsIsoFormat = getArguments["to-timestamp"];
    }

    if (getArguments.find("log-data-format") != getArguments.end())
    {
      const std::string format = getArguments["log-data-format"];
      if (format == "json")
      {
        logDataInJson = true;
      }
      else if (format == "base64")
      {
        logDataInJson = false;
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange, "Unsupported value for log-data-format: " + format);
      }
    }

    // MongoDB: "[]" if there is no log (upstream answers "null")
    Json::Value jsonLogs = Json::arrayValue;

#if ORTHANC_PLUGINS_HAS_AUDIT_LOGS == 1
    {
      std::list<IDatabaseBackend::AuditLog> logs;

      BaseIndexConnectionsPool::Accessor accessor(*connectionPool_);
      accessor.GetBackend().GetAuditLogs(accessor.GetManager(),
                                         logs,
                                         userIdFilter,
                                         resourceIdFilter,
                                         actionFilter,
                                         fromTsIsoFormat, toTsIsoFormat,
                                         since, limit);

      for (std::list<IDatabaseBackend::AuditLog>::const_iterator it = logs.begin(); it != logs.end(); ++it)
      {
        Json::Value serializedAuditLog;
        serializedAuditLog["SourcePlugin"] = it->GetSourcePlugin();
        serializedAuditLog["Timestamp"] = it->GetTimestamp();
        serializedAuditLog["UserId"] = it->GetUserId();
        serializedAuditLog["ResourceId"] = it->GetResourceId();
        serializedAuditLog["Action"] = it->GetAction();

        std::string level;
        switch (it->GetResourceType())
        {
          case OrthancPluginResourceType_Patient:
            level = "Patient";
            break;

          case OrthancPluginResourceType_Study:
            level = "Study";
            break;

          case OrthancPluginResourceType_Series:
            level = "Series";
            break;

          case OrthancPluginResourceType_Instance:
            level = "Instance";
            break;

          case OrthancPluginResourceType_None:
            level = "None";
            break;

          default:
            throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
        }

        serializedAuditLog["ResourceType"] = level;

        bool fillBase64;
        if (logDataInJson)
        {
          if (!it->HasLogData())
          {
            serializedAuditLog["JsonLogData"] = Json::nullValue;
            fillBase64 = false;
          }
          else
          {
            Json::Value logData;
            if (Orthanc::Toolbox::ReadJson(logData, it->GetLogData()))
            {
              serializedAuditLog["JsonLogData"] = logData;
              fillBase64 = false;
            }
            else
            {
              // If the data is not JSON compatible, export it in base64 anyway
              fillBase64 = true;
            }
          }
        }
        else
        {
          fillBase64 = true;
        }

        if (fillBase64)
        {
          if (it->HasLogData())
          {
            std::string b64;
            Orthanc::Toolbox::EncodeBase64(b64, it->GetLogData());
            serializedAuditLog["Base64LogData"] = b64;
          }
          else
          {
            serializedAuditLog["Base64LogData"] = Json::nullValue;
          }
        }

        jsonLogs.append(serializedAuditLog);
      }
    }
#else
    {
      // Disable warnings about unused variables if audit logs are unavailable in the SDK
      (void) since;
      (void) limit;
      (void) fromTsIsoFormat;
      (void) toTsIsoFormat;
      (void) logDataInJson;
    }
#endif

    OrthancPlugins::AnswerJson(jsonLogs, output);
  }


  OrthancPluginErrorCode OnChangeCallback(OrthancPluginChangeType changeType,
                                          OrthancPluginResourceType resourceType,
                                          const char* resourceId)
  {
    switch (changeType)
    {
      case OrthancPluginChangeType_OrthancStarted:
        DatabaseBackendAdapterV4::SetOrthancStarted();
        break;
      default:
        break;
    }

    return OrthancPluginErrorCode_Success;
  }

  void DatabaseBackendAdapterV4::Register(IndexBackend* backend,
                                          size_t countConnections,
                                          bool useDynamicConnectionPool,
                                          unsigned int maxDatabaseRetries,
                                          unsigned int housekeepingDelaySeconds)
  {
    // The "connectionPool_" takes the ownership of the backend
    if (useDynamicConnectionPool)
    {
      connectionPool_.reset(new DynamicIndexConnectionsPool(backend, countConnections, housekeepingDelaySeconds));
    }
    else
    {
      connectionPool_.reset(new IndexConnectionsPool(backend, countConnections, housekeepingDelaySeconds));
    }
    
    if (isBackendInUse_)
    {
      connectionPool_.reset();  // This implies "delete backend"
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }

    OrthancPluginContext* context = backend->GetContext();
 
    if (OrthancPluginRegisterDatabaseBackendV4(context, connectionPool_.get(), maxDatabaseRetries,
                                               CallBackend, FinalizeBackend) != OrthancPluginErrorCode_Success)
    {
      connectionPool_.reset();  // This implies "delete backend"
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError, "Unable to register the database backend");
    }

    isBackendInUse_ = true;

#if ORTHANC_PLUGINS_HAS_AUDIT_LOGS == 1
    // MongoDB: only register the handler if the back-end stores audit logs
    if (backend->HasAuditLogs())
    {
    OrthancPluginRegisterAuditLogHandler(context, AuditLogHandler);
    // MongoDB: the route is named after this plugin (upstream: "/plugins/postgresql/audit-logs")
    OrthancPlugins::RegisterRestCallback<GetAuditLogs>("/plugins/mongodb/audit-logs", true);
    }
#endif

    OrthancPluginRegisterOnChangeCallback(context, OnChangeCallback);
  }


  void DatabaseBackendAdapterV4::Finalize()
  {
    if (isBackendInUse_)
    {
      LOG(ERROR) << "The Orthanc core has not destructed the index backend, internal error";
    }
  }

  void DatabaseBackendAdapterV4::SetOrthancStarted()
  {
    if (!isBackendInUse_)
    {
      LOG(ERROR) << "The Orthanc core has not created the index backend, internal error";
      return;
    }
    
    BaseIndexConnectionsPool::Accessor accessor(*connectionPool_);
    accessor.GetBackend().SetOrthancStarted();
  }
}

#  endif
#endif
