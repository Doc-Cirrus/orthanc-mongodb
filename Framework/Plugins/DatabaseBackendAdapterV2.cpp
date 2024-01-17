// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "DatabaseBackendAdapterV2.h"
#include "GlobalProperties.h"

#include "IndexBackend.h"

#include <OrthancException.h>

#include <boost/thread/mutex.hpp>
#include <list>
#include <stdexcept>
#include <string>


#define ORTHANC_PLUGINS_DATABASE_CATCH                                  \
  catch (::Orthanc::OrthancException& e)                                \
  {                                                                     \
    return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());       \
  }                                                                     \
  catch (::std::runtime_error& e)                                       \
  {                                                                     \
    LogError(adapter->GetBackend(), e);                                 \
    return OrthancPluginErrorCode_DatabasePlugin;                       \
  }                                                                     \
  catch (...)                                                           \
  {                                                                     \
    OrthancPluginLogError(adapter->GetBackend().GetContext(), "Native exception"); \
    return OrthancPluginErrorCode_DatabasePlugin;                       \
  }


namespace OrthancDatabases
{
  class DatabaseBackendAdapterV2::Adapter : public boost::noncopyable
  {
  private:
    /**
     * The "managerMutex_" should not be necessary, as it is redundant
     * with with "Orthanc::ServerIndex::mutex_" (the global mutex) in
     * Orthanc <= 1.9.1, or with
     * "Orthanc::OrthancPluginDatabase::mutex_" in Orthanc >= 1.9.2
     * (the global mutex limited to backward compatibility with older
     * plugins). It is left here for additional safety.
     **/

    std::unique_ptr<IDatabaseBackend>  backend_;
    boost::mutex                       managerMutex_;
    std::unique_ptr<DatabaseManager>   manager_;

  public:
    explicit Adapter(IDatabaseBackend* backend) :
      backend_(backend)
    {
      if (backend == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
      }
    }

    IDatabaseBackend& GetBackend() const
    {
      return *backend_;
    }

    void OpenConnection()
    {
      boost::mutex::scoped_lock  lock(managerMutex_);

      if (manager_.get() == NULL)
      {
        manager_.reset(IndexBackend::CreateSingleDatabaseManager(*backend_));
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }

    void CloseConnection()
    {
      boost::mutex::scoped_lock  lock(managerMutex_);

      if (manager_.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      else
      {
        manager_->Close();
        manager_.reset(NULL);
      }
    }

    class DatabaseAccessor : public boost::noncopyable
    {
    private:
      boost::mutex::scoped_lock  lock_;
      DatabaseManager*           manager_;

    public:
      explicit DatabaseAccessor(Adapter& adapter) :
        lock_(adapter.managerMutex_),
        manager_(adapter.manager_.get())
      {
        if (manager_ == NULL)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
      }

      DatabaseManager& GetManager() const
      {
        assert(manager_ != NULL);
        return *manager_;
      }
    };
  };


  class DatabaseBackendAdapterV2::Output : public IDatabaseBackendOutput
  {
  public:
    enum AllowedAnswers
    {
      AllowedAnswers_All,
      AllowedAnswers_None,
      AllowedAnswers_Attachment,
      AllowedAnswers_Change,
      AllowedAnswers_DicomTag,
      AllowedAnswers_ExportedResource,
      AllowedAnswers_MatchingResource,
      AllowedAnswers_String,
      AllowedAnswers_Metadata
    };

  private:
    OrthancPluginContext*         context_;
    OrthancPluginDatabaseContext* database_;
    AllowedAnswers                allowedAnswers_;

  public:
    Output(OrthancPluginContext*         context,
           OrthancPluginDatabaseContext* database) :
      context_(context),
      database_(database),
      allowedAnswers_(AllowedAnswers_All /* for unit tests */)
    {
    }

    void SetAllowedAnswers(AllowedAnswers allowed)
    {
      allowedAnswers_ = allowed;
    }

    OrthancPluginDatabaseContext* GetDatabase() const
    {
      return database_;
    }

    virtual void SignalDeletedAttachment(const std::string& uuid,
                                         int32_t            contentType,
                                         uint64_t           uncompressedSize,
                                         const std::string& uncompressedHash,
                                         int32_t            compressionType,
                                         uint64_t           compressedSize,
                                         const std::string& compressedHash) ORTHANC_OVERRIDE
    {
      OrthancPluginAttachment attachment;
      attachment.uuid = uuid.c_str();
      attachment.contentType = contentType;
      attachment.uncompressedSize = uncompressedSize;
      attachment.uncompressedHash = uncompressedHash.c_str();
      attachment.compressionType = compressionType;
      attachment.compressedSize = compressedSize;
      attachment.compressedHash = compressedHash.c_str();

      OrthancPluginDatabaseSignalDeletedAttachment(context_, database_, &attachment);
    }

    virtual void SignalDeletedResource(const std::string& publicId,
                                       OrthancPluginResourceType resourceType) ORTHANC_OVERRIDE
    {
      OrthancPluginDatabaseSignalDeletedResource(context_, database_, publicId.c_str(), resourceType);
    }

    virtual void SignalRemainingAncestor(const std::string& ancestorId,
                                         OrthancPluginResourceType ancestorType) ORTHANC_OVERRIDE
    {
      OrthancPluginDatabaseSignalRemainingAncestor(context_, database_, ancestorId.c_str(), ancestorType);
    }

    virtual void AnswerAttachment(const std::string& uuid,
                                  int32_t            contentType,
                                  uint64_t           uncompressedSize,
                                  const std::string& uncompressedHash,
                                  int32_t            compressionType,
                                  uint64_t           compressedSize,
                                  const std::string& compressedHash) ORTHANC_OVERRIDE
    {
      if (allowedAnswers_ != AllowedAnswers_All &&
          allowedAnswers_ != AllowedAnswers_Attachment)
      {
        throw std::runtime_error("Cannot answer with an attachment in the current state");
      }

      OrthancPluginAttachment attachment;
      attachment.uuid = uuid.c_str();
      attachment.contentType = contentType;
      attachment.uncompressedSize = uncompressedSize;
      attachment.uncompressedHash = uncompressedHash.c_str();
      attachment.compressionType = compressionType;
      attachment.compressedSize = compressedSize;
      attachment.compressedHash = compressedHash.c_str();

      OrthancPluginDatabaseAnswerAttachment(context_, database_, &attachment);
    }

    virtual void AnswerChange(int64_t                    seq,
                              int32_t                    changeType,
                              OrthancPluginResourceType  resourceType,
                              const std::string&         publicId,
                              const std::string&         date) ORTHANC_OVERRIDE
    {
      if (allowedAnswers_ != AllowedAnswers_All &&
          allowedAnswers_ != AllowedAnswers_Change)
      {
        throw std::runtime_error("Cannot answer with a change in the current state");
      }

      OrthancPluginChange change;
      change.seq = seq;
      change.changeType = changeType;
      change.resourceType = resourceType;
      change.publicId = publicId.c_str();
      change.date = date.c_str();

      OrthancPluginDatabaseAnswerChange(context_, database_, &change);
    }

    virtual void AnswerDicomTag(uint16_t group,
                                uint16_t element,
                                const std::string& value) ORTHANC_OVERRIDE
    {
      if (allowedAnswers_ != AllowedAnswers_All &&
          allowedAnswers_ != AllowedAnswers_DicomTag)
      {
        throw std::runtime_error("Cannot answer with a DICOM tag in the current state");
      }

      OrthancPluginDicomTag tag;
      tag.group = group;
      tag.element = element;
      tag.value = value.c_str();

      OrthancPluginDatabaseAnswerDicomTag(context_, database_, &tag);
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
      if (allowedAnswers_ != AllowedAnswers_All &&
          allowedAnswers_ != AllowedAnswers_ExportedResource)
      {
        throw std::runtime_error("Cannot answer with an exported resource in the current state");
      }

      OrthancPluginExportedResource exported;
      exported.seq = seq;
      exported.resourceType = resourceType;
      exported.publicId = publicId.c_str();
      exported.modality = modality.c_str();
      exported.date = date.c_str();
      exported.patientId = patientId.c_str();
      exported.studyInstanceUid = studyInstanceUid.c_str();
      exported.seriesInstanceUid = seriesInstanceUid.c_str();
      exported.sopInstanceUid = sopInstanceUid.c_str();

      OrthancPluginDatabaseAnswerExportedResource(context_, database_, &exported);
    }


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void AnswerMatchingResource(const std::string& resourceId) ORTHANC_OVERRIDE
    {
      if (allowedAnswers_ != AllowedAnswers_All &&
          allowedAnswers_ != AllowedAnswers_MatchingResource)
      {
        throw std::runtime_error("Cannot answer with an exported resource in the current state");
      }

      OrthancPluginMatchingResource match;
      match.resourceId = resourceId.c_str();
      match.someInstanceId = NULL;

      OrthancPluginDatabaseAnswerMatchingResource(context_, database_, &match);
    }
#endif


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void AnswerMatchingResource(const std::string& resourceId,
                                        const std::string& someInstanceId) ORTHANC_OVERRIDE
    {
      if (allowedAnswers_ != AllowedAnswers_All &&
          allowedAnswers_ != AllowedAnswers_MatchingResource)
      {
        throw std::runtime_error("Cannot answer with an exported resource in the current state");
      }

      OrthancPluginMatchingResource match;
      match.resourceId = resourceId.c_str();
      match.someInstanceId = someInstanceId.c_str();

      OrthancPluginDatabaseAnswerMatchingResource(context_, database_, &match);
    }
#endif
  };


  static void LogError(IDatabaseBackend& backend,
                       const std::runtime_error& e)
  {
    const std::string message = "Exception in database back-end: " + std::string(e.what());
    OrthancPluginLogError(backend.GetContext(), message.c_str());
  }


  static OrthancPluginErrorCode  AddAttachment(void* payload,
                                               int64_t id,
                                               const OrthancPluginAttachment* attachment)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().AddAttachment(accessor.GetManager(), id, *attachment,
                                          0 /* revision number, unused in old API */);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  AttachChild(void* payload,
                                             int64_t parent,
                                             int64_t child)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().AttachChild(accessor.GetManager(), parent, child);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  ClearChanges(void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().ClearChanges(accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  ClearExportedResources(void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().ClearExportedResources(accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  CreateResource(int64_t* id,
                                                void* payload,
                                                const char* publicId,
                                                OrthancPluginResourceType resourceType)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      *id = adapter->GetBackend().CreateResource(accessor.GetManager(), publicId, resourceType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  DeleteAttachment(void* payload,
                                                  int64_t id,
                                                  int32_t contentType)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().DeleteAttachment(*output, accessor.GetManager(), id, contentType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  DeleteMetadata(void* payload,
                                                int64_t id,
                                                int32_t metadataType)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().DeleteMetadata(accessor.GetManager(), id, metadataType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  DeleteResource(void* payload,
                                                int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().DeleteResource(*output, accessor.GetManager(), id);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetAllInternalIds(OrthancPluginDatabaseContext* context,
                                                   void* payload,
                                                   OrthancPluginResourceType resourceType)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<int64_t> target;
      adapter->GetBackend().GetAllInternalIds(target, accessor.GetManager(), resourceType);

      for (std::list<int64_t>::const_iterator
             it = target.begin(); it != target.end(); ++it)
      {
        OrthancPluginDatabaseAnswerInt64(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(), *it);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetAllPublicIds(OrthancPluginDatabaseContext* context,
                                                 void* payload,
                                                 OrthancPluginResourceType resourceType)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<std::string> ids;
      adapter->GetBackend().GetAllPublicIds(ids, accessor.GetManager(), resourceType);

      for (std::list<std::string>::const_iterator
             it = ids.begin(); it != ids.end(); ++it)
      {
        OrthancPluginDatabaseAnswerString(adapter->GetBackend().GetContext(),
                                          output->GetDatabase(),
                                          it->c_str());
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetAllPublicIdsWithLimit(OrthancPluginDatabaseContext* context,
                                                          void* payload,
                                                          OrthancPluginResourceType resourceType,
                                                          uint64_t since,
                                                          uint64_t limit)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<std::string> ids;
      adapter->GetBackend().GetAllPublicIds(ids, accessor.GetManager(), resourceType, since, limit);

      for (std::list<std::string>::const_iterator
             it = ids.begin(); it != ids.end(); ++it)
      {
        OrthancPluginDatabaseAnswerString(adapter->GetBackend().GetContext(),
                                          output->GetDatabase(),
                                          it->c_str());
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetChanges(OrthancPluginDatabaseContext* context,
                                            void* payload,
                                            int64_t since,
                                            uint32_t maxResult)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_Change);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      bool done;
      adapter->GetBackend().GetChanges(*output, done, accessor.GetManager(), since, maxResult);

      if (done)
      {
        OrthancPluginDatabaseAnswerChangesDone(adapter->GetBackend().GetContext(),
                                               output->GetDatabase());
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetChildrenInternalId(OrthancPluginDatabaseContext* context,
                                                       void* payload,
                                                       int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<int64_t> target;
      adapter->GetBackend().GetChildrenInternalId(target, accessor.GetManager(), id);

      for (std::list<int64_t>::const_iterator
             it = target.begin(); it != target.end(); ++it)
      {
        OrthancPluginDatabaseAnswerInt64(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(), *it);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetChildrenPublicId(OrthancPluginDatabaseContext* context,
                                                     void* payload,
                                                     int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<std::string> ids;
      adapter->GetBackend().GetChildrenPublicId(ids, accessor.GetManager(), id);

      for (std::list<std::string>::const_iterator
             it = ids.begin(); it != ids.end(); ++it)
      {
        OrthancPluginDatabaseAnswerString(adapter->GetBackend().GetContext(),
                                          output->GetDatabase(),
                                          it->c_str());
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetExportedResources(OrthancPluginDatabaseContext* context,
                                                      void* payload,
                                                      int64_t  since,
                                                      uint32_t  maxResult)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_ExportedResource);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      bool done;
      adapter->GetBackend().GetExportedResources(*output, done, accessor.GetManager(), since, maxResult);

      if (done)
      {
        OrthancPluginDatabaseAnswerExportedResourcesDone(adapter->GetBackend().GetContext(),
                                                         output->GetDatabase());
      }
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetLastChange(OrthancPluginDatabaseContext* context,
                                               void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_Change);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().GetLastChange(*output, accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetLastExportedResource(OrthancPluginDatabaseContext* context,
                                                         void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_ExportedResource);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().GetLastExportedResource(*output, accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetMainDicomTags(OrthancPluginDatabaseContext* context,
                                                  void* payload,
                                                  int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_DicomTag);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().GetMainDicomTags(*output, accessor.GetManager(), id);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetPublicId(OrthancPluginDatabaseContext* context,
                                             void* payload,
                                             int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::string s = adapter->GetBackend().GetPublicId(accessor.GetManager(), id);
      OrthancPluginDatabaseAnswerString(adapter->GetBackend().GetContext(),
                                        output->GetDatabase(),
                                        s.c_str());

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetResourceCount(uint64_t* target,
                                                  void* payload,
                                                  OrthancPluginResourceType  resourceType)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      *target = adapter->GetBackend().GetResourcesCount(accessor.GetManager(), resourceType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetResourceType(OrthancPluginResourceType* resourceType,
                                                 void* payload,
                                                 int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      *resourceType = adapter->GetBackend().GetResourceType(accessor.GetManager(), id);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetTotalCompressedSize(uint64_t* target,
                                                        void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      *target = adapter->GetBackend().GetTotalCompressedSize(accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  GetTotalUncompressedSize(uint64_t* target,
                                                          void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      *target = adapter->GetBackend().GetTotalUncompressedSize(accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  IsExistingResource(int32_t* existing,
                                                    void* payload,
                                                    int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      *existing = adapter->GetBackend().IsExistingResource(accessor.GetManager(), id);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  IsProtectedPatient(int32_t* isProtected,
                                                    void* payload,
                                                    int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      *isProtected = adapter->GetBackend().IsProtectedPatient(accessor.GetManager(), id);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  ListAvailableMetadata(OrthancPluginDatabaseContext* context,
                                                       void* payload,
                                                       int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<int32_t> target;
      adapter->GetBackend().ListAvailableMetadata(target, accessor.GetManager(), id);

      for (std::list<int32_t>::const_iterator
             it = target.begin(); it != target.end(); ++it)
      {
        OrthancPluginDatabaseAnswerInt32(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(),
                                         *it);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  ListAvailableAttachments(OrthancPluginDatabaseContext* context,
                                                          void* payload,
                                                          int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<int32_t> target;
      adapter->GetBackend().ListAvailableAttachments(target, accessor.GetManager(), id);

      for (std::list<int32_t>::const_iterator
             it = target.begin(); it != target.end(); ++it)
      {
        OrthancPluginDatabaseAnswerInt32(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(),
                                         *it);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  LogChange(void* payload,
                                           const OrthancPluginChange* change)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      int64_t id;
      OrthancPluginResourceType type;
      if (!adapter->GetBackend().LookupResource(id, type, accessor.GetManager(), change->publicId) ||
          type != change->resourceType)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
      }
      else
      {
        adapter->GetBackend().LogChange(accessor.GetManager(), change->changeType, id, type, change->date);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  LogExportedResource(void* payload,
                                                     const OrthancPluginExportedResource* exported)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().LogExportedResource(accessor.GetManager(), *exported);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  LookupAttachment(OrthancPluginDatabaseContext* context,
                                                  void* payload,
                                                  int64_t id,
                                                  int32_t contentType)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_Attachment);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      int64_t revision;  // not handled in this API
      adapter->GetBackend().LookupAttachment(*output, revision, accessor.GetManager(), id, contentType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  LookupGlobalProperty(OrthancPluginDatabaseContext* context,
                                                      void* payload,
                                                      int32_t property)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::string s;
      if (adapter->GetBackend().LookupGlobalProperty(s, accessor.GetManager(), MISSING_SERVER_IDENTIFIER, property))
      {
        OrthancPluginDatabaseAnswerString(adapter->GetBackend().GetContext(),
                                          output->GetDatabase(),
                                          s.c_str());
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  LookupIdentifier3(OrthancPluginDatabaseContext* context,
                                                   void* payload,
                                                   OrthancPluginResourceType resourceType,
                                                   const OrthancPluginDicomTag* tag,
                                                   OrthancPluginIdentifierConstraint constraint)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<int64_t> target;
      adapter->GetBackend().LookupIdentifier(target, accessor.GetManager(), resourceType,
                                             tag->group, tag->element, constraint, tag->value);

      for (std::list<int64_t>::const_iterator
             it = target.begin(); it != target.end(); ++it)
      {
        OrthancPluginDatabaseAnswerInt64(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(), *it);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 4, 0)
  static OrthancPluginErrorCode  LookupIdentifierRange(OrthancPluginDatabaseContext* context,
                                                       void* payload,
                                                       OrthancPluginResourceType resourceType,
                                                       uint16_t group,
                                                       uint16_t element,
                                                       const char* start,
                                                       const char* end)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<int64_t> target;
      adapter->GetBackend().LookupIdentifierRange(target, accessor.GetManager(), resourceType, group, element, start, end);

      for (std::list<int64_t>::const_iterator
             it = target.begin(); it != target.end(); ++it)
      {
        OrthancPluginDatabaseAnswerInt64(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(), *it);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#  endif
#endif


  static OrthancPluginErrorCode  LookupMetadata(OrthancPluginDatabaseContext* context,
                                                void* payload,
                                                int64_t id,
                                                int32_t metadata)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::string s;
      int64_t revision;  // not handled in this API
      if (adapter->GetBackend().LookupMetadata(s, revision, accessor.GetManager(), id, metadata))
      {
        OrthancPluginDatabaseAnswerString(adapter->GetBackend().GetContext(),
                                          output->GetDatabase(), s.c_str());
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  LookupParent(OrthancPluginDatabaseContext* context,
                                              void* payload,
                                              int64_t id)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      int64_t parent;
      if (adapter->GetBackend().LookupParent(parent, accessor.GetManager(), id))
      {
        OrthancPluginDatabaseAnswerInt64(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(), parent);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  LookupResource(OrthancPluginDatabaseContext* context,
                                                void* payload,
                                                const char* publicId)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      int64_t id;
      OrthancPluginResourceType type;
      if (adapter->GetBackend().LookupResource(id, type, accessor.GetManager(), publicId))
      {
        OrthancPluginDatabaseAnswerResource(adapter->GetBackend().GetContext(),
                                            output->GetDatabase(),
                                            id, type);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  SelectPatientToRecycle(OrthancPluginDatabaseContext* context,
                                                        void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      int64_t id;
      if (adapter->GetBackend().SelectPatientToRecycle(id, accessor.GetManager()))
      {
        OrthancPluginDatabaseAnswerInt64(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(), id);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  SelectPatientToRecycle2(OrthancPluginDatabaseContext* context,
                                                         void* payload,
                                                         int64_t patientIdToAvoid)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      int64_t id;
      if (adapter->GetBackend().SelectPatientToRecycle(id, accessor.GetManager(), patientIdToAvoid))
      {
        OrthancPluginDatabaseAnswerInt64(adapter->GetBackend().GetContext(),
                                         output->GetDatabase(), id);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  SetGlobalProperty(void* payload,
                                                   int32_t property,
                                                   const char* value)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().SetGlobalProperty(accessor.GetManager(), MISSING_SERVER_IDENTIFIER, property, value);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  SetMainDicomTag(void* payload,
                                                 int64_t id,
                                                 const OrthancPluginDicomTag* tag)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().SetMainDicomTag(accessor.GetManager(), id, tag->group, tag->element, tag->value);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  SetIdentifierTag(void* payload,
                                                  int64_t id,
                                                  const OrthancPluginDicomTag* tag)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().SetIdentifierTag(accessor.GetManager(), id, tag->group, tag->element, tag->value);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  SetMetadata(void* payload,
                                             int64_t id,
                                             int32_t metadata,
                                             const char* value)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().SetMetadata(accessor.GetManager(), id, metadata, value,
                                        0 /* revision number, unused in old API */);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode  SetProtectedPatient(void* payload,
                                                     int64_t id,
                                                     int32_t isProtected)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().SetProtectedPatient(accessor.GetManager(), id, (isProtected != 0));
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode StartTransaction(void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      accessor.GetManager().StartTransaction(TransactionType_ReadWrite);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode RollbackTransaction(void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      accessor.GetManager().RollbackTransaction();
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode CommitTransaction(void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      accessor.GetManager().CommitTransaction();
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode Open(void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      adapter->OpenConnection();
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode Close(void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      adapter->CloseConnection();
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode GetDatabaseVersion(uint32_t* version,
                                                   void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      *version = adapter->GetBackend().GetDatabaseVersion(accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode UpgradeDatabase(void* payload,
                                                uint32_t  targetVersion,
                                                OrthancPluginStorageArea* storageArea)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().UpgradeDatabase(accessor.GetManager(), targetVersion, storageArea);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode ClearMainDicomTags(void* payload,
                                                   int64_t internalId)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().ClearMainDicomTags(accessor.GetManager(), internalId);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  /* Use GetOutput().AnswerResource() */
  static OrthancPluginErrorCode LookupResources(
    OrthancPluginDatabaseContext* context,
    void* payload,
    uint32_t constraintsCount,
    const OrthancPluginDatabaseConstraint* constraints,
    OrthancPluginResourceType queryLevel,
    uint32_t limit,
    uint8_t requestSomeInstance)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_MatchingResource);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::vector<Orthanc::DatabaseConstraint> lookup;
      lookup.reserve(constraintsCount);

      for (uint32_t i = 0; i < constraintsCount; i++)
      {
        lookup.push_back(Orthanc::DatabaseConstraint(constraints[i]));
      }

      adapter->GetBackend().LookupResources(*output, accessor.GetManager(), lookup, queryLevel, limit, (requestSomeInstance != 0));
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#endif


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  static OrthancPluginErrorCode CreateInstance(OrthancPluginCreateInstanceResult* target,
                                               void* payload,
                                               const char* hashPatient,
                                               const char* hashStudy,
                                               const char* hashSeries,
                                               const char* hashInstance)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().CreateInstance(*target, accessor.GetManager(), hashPatient, hashStudy, hashSeries, hashInstance);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#endif


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  static OrthancPluginErrorCode SetResourcesContent(
    void* payload,
    uint32_t countIdentifierTags,
    const OrthancPluginResourcesContentTags* identifierTags,
    uint32_t countMainDicomTags,
    const OrthancPluginResourcesContentTags* mainDicomTags,
    uint32_t countMetadata,
    const OrthancPluginResourcesContentMetadata* metadata)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().SetResourcesContent(accessor.GetManager(), countIdentifierTags, identifierTags,
                                                countMainDicomTags, mainDicomTags, countMetadata, metadata);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#endif


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 2)
  // New primitive since Orthanc 1.5.2
  static OrthancPluginErrorCode GetChildrenMetadata(OrthancPluginDatabaseContext* context,
                                                    void* payload,
                                                    int64_t resourceId,
                                                    int32_t metadata)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_None);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::list<std::string> values;
      adapter->GetBackend().GetChildrenMetadata(values, accessor.GetManager(), resourceId, metadata);

      for (std::list<std::string>::const_iterator
             it = values.begin(); it != values.end(); ++it)
      {
        OrthancPluginDatabaseAnswerString(adapter->GetBackend().GetContext(),
                                          output->GetDatabase(),
                                          it->c_str());
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#  endif
#endif


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 2)
  // New primitive since Orthanc 1.5.2
  static OrthancPluginErrorCode GetLastChangeIndex(int64_t* result,
                                                   void* payload)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      *result = adapter->GetBackend().GetLastChangeIndex(accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#  endif
#endif


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 2)
  // New primitive since Orthanc 1.5.2
  static OrthancPluginErrorCode TagMostRecentPatient(void* payload,
                                                     int64_t patientId)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);
      adapter->GetBackend().TagMostRecentPatient(accessor.GetManager(), patientId);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#  endif
#endif


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
  // New primitive since Orthanc 1.5.4
  static OrthancPluginErrorCode GetAllMetadata(OrthancPluginDatabaseContext* context,
                                               void* payload,
                                               int64_t resourceId)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_Metadata);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::map<int32_t, std::string> result;
      adapter->GetBackend().GetAllMetadata(result, accessor.GetManager(), resourceId);

      for (std::map<int32_t, std::string>::const_iterator
             it = result.begin(); it != result.end(); ++it)
      {
        OrthancPluginDatabaseAnswerMetadata(adapter->GetBackend().GetContext(),
                                            output->GetDatabase(),
                                            resourceId, it->first, it->second.c_str());
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#  endif
#endif


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
  // New primitive since Orthanc 1.5.4
  static OrthancPluginErrorCode LookupResourceAndParent(OrthancPluginDatabaseContext* context,
                                                        uint8_t* isExisting,
                                                        int64_t* id,
                                                        OrthancPluginResourceType* type,
                                                        void* payload,
                                                        const char* publicId)
  {
    DatabaseBackendAdapterV2::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV2::Adapter*>(payload);
    std::unique_ptr<DatabaseBackendAdapterV2::Output> output(dynamic_cast<DatabaseBackendAdapterV2::Output*>(adapter->GetBackend().CreateOutput()));
    output->SetAllowedAnswers(DatabaseBackendAdapterV2::Output::AllowedAnswers_String);

    try
    {
      DatabaseBackendAdapterV2::Adapter::DatabaseAccessor accessor(*adapter);

      std::string parent;
      if (adapter->GetBackend().LookupResourceAndParent(*id, *type, parent, accessor.GetManager(), publicId))
      {
        *isExisting = 1;

        if (!parent.empty())
        {
          OrthancPluginDatabaseAnswerString(adapter->GetBackend().GetContext(),
                                            output->GetDatabase(),
                                            parent.c_str());
        }
      }
      else
      {
        *isExisting = 0;
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#  endif
#endif


  IDatabaseBackendOutput* DatabaseBackendAdapterV2::Factory::CreateOutput()
  {
    return new Output(context_, database_);
  }


  static std::unique_ptr<DatabaseBackendAdapterV2::Adapter> adapter_;

  void DatabaseBackendAdapterV2::Register(IDatabaseBackend* backend)
  {
    if (backend == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    if (adapter_.get() != NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }

    adapter_.reset(new Adapter(backend));

    OrthancPluginDatabaseBackend  params;
    memset(&params, 0, sizeof(params));

    OrthancPluginDatabaseExtensions  extensions;
    memset(&extensions, 0, sizeof(extensions));

    params.addAttachment = AddAttachment;
    params.attachChild = AttachChild;
    params.clearChanges = ClearChanges;
    params.clearExportedResources = ClearExportedResources;
    params.createResource = CreateResource;
    params.deleteAttachment = DeleteAttachment;
    params.deleteMetadata = DeleteMetadata;
    params.deleteResource = DeleteResource;
    params.getAllPublicIds = GetAllPublicIds;
    params.getChanges = GetChanges;
    params.getChildrenInternalId = GetChildrenInternalId;
    params.getChildrenPublicId = GetChildrenPublicId;
    params.getExportedResources = GetExportedResources;
    params.getLastChange = GetLastChange;
    params.getLastExportedResource = GetLastExportedResource;
    params.getMainDicomTags = GetMainDicomTags;
    params.getPublicId = GetPublicId;
    params.getResourceCount = GetResourceCount;
    params.getResourceType = GetResourceType;
    params.getTotalCompressedSize = GetTotalCompressedSize;
    params.getTotalUncompressedSize = GetTotalUncompressedSize;
    params.isExistingResource = IsExistingResource;
    params.isProtectedPatient = IsProtectedPatient;
    params.listAvailableMetadata = ListAvailableMetadata;
    params.listAvailableAttachments = ListAvailableAttachments;
    params.logChange = LogChange;
    params.logExportedResource = LogExportedResource;
    params.lookupAttachment = LookupAttachment;
    params.lookupGlobalProperty = LookupGlobalProperty;
    params.lookupIdentifier = NULL;    // Unused starting with Orthanc 0.9.5 (db v6)
    params.lookupIdentifier2 = NULL;   // Unused starting with Orthanc 0.9.5 (db v6)
    params.lookupMetadata = LookupMetadata;
    params.lookupParent = LookupParent;
    params.lookupResource = LookupResource;
    params.selectPatientToRecycle = SelectPatientToRecycle;
    params.selectPatientToRecycle2 = SelectPatientToRecycle2;
    params.setGlobalProperty = SetGlobalProperty;
    params.setMainDicomTag = SetMainDicomTag;
    params.setIdentifierTag = SetIdentifierTag;
    params.setMetadata = SetMetadata;
    params.setProtectedPatient = SetProtectedPatient;
    params.startTransaction = StartTransaction;
    params.rollbackTransaction = RollbackTransaction;
    params.commitTransaction = CommitTransaction;
    params.open = Open;
    params.close = Close;

    extensions.getAllPublicIdsWithLimit = GetAllPublicIdsWithLimit;
    extensions.getDatabaseVersion = GetDatabaseVersion;
    extensions.upgradeDatabase = UpgradeDatabase;
    extensions.clearMainDicomTags = ClearMainDicomTags;
    extensions.getAllInternalIds = GetAllInternalIds;     // New in Orthanc 0.9.5 (db v6)
    extensions.lookupIdentifier3 = LookupIdentifier3;     // New in Orthanc 0.9.5 (db v6)

    bool performanceWarning = true;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)         // Macro introduced in Orthanc 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 4, 0)
    extensions.lookupIdentifierRange = LookupIdentifierRange;    // New in Orthanc 1.4.0
#  endif
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    // Optimizations brought by Orthanc 1.5.2
    extensions.lookupResources = LookupResources;          // Fast lookup
    extensions.setResourcesContent = SetResourcesContent;  // Fast setting tags/metadata
    extensions.getChildrenMetadata = GetChildrenMetadata;
    extensions.getLastChangeIndex = GetLastChangeIndex;
    extensions.tagMostRecentPatient = TagMostRecentPatient;

    if (adapter_->GetBackend().HasCreateInstance())
    {
      extensions.createInstance = CreateInstance;          // Fast creation of resources
    }
#endif

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
    // Optimizations brought by Orthanc 1.5.4
    extensions.lookupResourceAndParent = LookupResourceAndParent;
    extensions.getAllMetadata = GetAllMetadata;
    performanceWarning = false;
#  endif
#endif

    OrthancPluginContext* context = adapter_->GetBackend().GetContext();

    if (performanceWarning)
    {
      char info[1024];
      sprintf(info,
              "Performance warning: The database index plugin was compiled "
              "against an old version of the Orthanc SDK (%d.%d.%d): "
              "Consider upgrading to version %d.%d.%d of the Orthanc SDK",
              ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER,
              ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER,
              ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER,
              ORTHANC_OPTIMAL_VERSION_MAJOR,
              ORTHANC_OPTIMAL_VERSION_MINOR,
              ORTHANC_OPTIMAL_VERSION_REVISION);

      OrthancPluginLogWarning(context, info);
    }

    OrthancPluginDatabaseContext* database =
      OrthancPluginRegisterDatabaseBackendV2(context, &params, &extensions, adapter_.get());
    if (database == NULL)
    {
      throw std::runtime_error("Unable to register the database backend");
    }

    adapter_->GetBackend().SetOutputFactory(new Factory(context, database));
  }


  void DatabaseBackendAdapterV2::Finalize()
  {
    adapter_.reset(NULL);
  }
}
