// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "DatabaseBackendAdapterV3.h"

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)         // Macro introduced in Orthanc 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 9, 2)

#include <Logging.h>
#include <MultiThreading/SharedMessageQueue.h>
#include <OrthancException.h>

#include <stdexcept>
#include <list>
#include <string>
#include <cassert>


#define ORTHANC_PLUGINS_DATABASE_CATCH(context)                         \
  catch (::Orthanc::OrthancException& e)                                \
  {                                                                     \
    return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());       \
  }                                                                     \
  catch (::std::runtime_error& e)                                       \
  {                                                                     \
    const std::string message = "Exception in database back-end: " + std::string(e.what()); \
    OrthancPluginLogError(context, message.c_str());                    \
    return OrthancPluginErrorCode_DatabasePlugin;                       \
  }                                                                     \
  catch (...)                                                           \
  {                                                                     \
    OrthancPluginLogError(context, "Native exception");                 \
    return OrthancPluginErrorCode_DatabasePlugin;                       \
  }


namespace OrthancDatabases
{
  static bool isBackendInUse_ = false;  // Only for sanity checks


  template <typename T>
  static void CopyListToVector(std::vector<T>& target,
                               const std::list<T>& source)
  {
    /**
     * This has the the same effect as:
     *
     *   target.reserve(source.size());
     *   std::copy(std::begin(source), std::end(source), std::back_inserter(target));
     *
     * However, this implementation is compatible with C++03 (Linux
     * Standard Base), whereas "std::back_inserter" requires C++11.
     **/

    target.clear();
    target.reserve(source.size());

    for (typename std::list<T>::const_iterator it = source.begin(); it != source.end(); ++it)
    {
      target.push_back(*it);
    }
  }


  class DatabaseBackendAdapterV3::Adapter : public boost::noncopyable
  {
  private:
    class ManagerReference : public Orthanc::IDynamicObject
    {
    private:
      DatabaseManager*  manager_;

    public:
      ManagerReference(DatabaseManager& manager) :
        manager_(&manager)
      {
      }

      DatabaseManager& GetManager()
      {
        assert(manager_ != NULL);
        return *manager_;
      }
    };

    std::unique_ptr<IndexBackend>  backend_;
    OrthancPluginContext*          context_;
    boost::shared_mutex            connectionsMutex_;
    size_t                         countConnections_;
    std::list<DatabaseManager*>    connections_;
    Orthanc::SharedMessageQueue    availableConnections_;

  public:
    Adapter(IndexBackend* backend,
            size_t countConnections) :
      backend_(backend),
      countConnections_(countConnections)
    {
      if (countConnections == 0)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                        "There must be a non-zero number of connections to the database");
      }
      else if (backend == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
      }
      else
      {
        context_ = backend_->GetContext();
      }
    }

    ~Adapter()
    {
      for (std::list<DatabaseManager*>::iterator
             it = connections_.begin(); it != connections_.end(); ++it)
      {
        assert(*it != NULL);
        delete *it;
      }
    }

    OrthancPluginContext* GetContext() const
    {
      return context_;
    }

    void OpenConnections()
    {
      boost::unique_lock<boost::shared_mutex>  lock(connectionsMutex_);

      if (connections_.size() == 0)
      {
        assert(backend_.get() != NULL);

        {
          std::unique_ptr<DatabaseManager> manager(new DatabaseManager(backend_->CreateDatabaseFactory()));
          manager->GetDatabase();  // Make sure to open the database connection

          backend_->ConfigureDatabase(*manager);
          connections_.push_back(manager.release());
        }

        for (size_t i = 1; i < countConnections_; i++)
        {
          connections_.push_back(new DatabaseManager(backend_->CreateDatabaseFactory()));
          connections_.back()->GetDatabase();  // Make sure to open the database connection
        }

        for (std::list<DatabaseManager*>::iterator
               it = connections_.begin(); it != connections_.end(); ++it)
        {
          assert(*it != NULL);
          availableConnections_.Enqueue(new ManagerReference(**it));
        }
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }

    void CloseConnections()
    {
      boost::unique_lock<boost::shared_mutex>  lock(connectionsMutex_);

      if (connections_.size() != countConnections_)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      else if (availableConnections_.GetSize() != countConnections_)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database, "Some connections are still in use, bug in the Orthanc core");
      }
      else
      {
        for (std::list<DatabaseManager*>::iterator
               it = connections_.begin(); it != connections_.end(); ++it)
        {
          assert(*it != NULL);
          (*it)->Close();
        }
      }
    }

    class DatabaseAccessor : public boost::noncopyable
    {
    private:
      boost::shared_lock<boost::shared_mutex>  lock_;
      Adapter&                                 adapter_;
      DatabaseManager*                         manager_;

    public:
      DatabaseAccessor(Adapter& adapter) :
        lock_(adapter.connectionsMutex_),
        adapter_(adapter),
        manager_(NULL)
      {
        for (;;)
        {
          std::unique_ptr<Orthanc::IDynamicObject> manager(adapter.availableConnections_.Dequeue(100));
          if (manager.get() != NULL)
          {
            manager_ = &dynamic_cast<ManagerReference&>(*manager).GetManager();
            return;
          }
        }
      }

      ~DatabaseAccessor()
      {
        assert(manager_ != NULL);
        adapter_.availableConnections_.Enqueue(new ManagerReference(*manager_));
      }

      IndexBackend& GetBackend() const
      {
        return *adapter_.backend_;
      }

      DatabaseManager& GetManager() const
      {
        assert(manager_ != NULL);
        return *manager_;
      }
    };
  };


  class DatabaseBackendAdapterV3::Output : public IDatabaseBackendOutput
  {
  private:
    struct Metadata
    {
      int32_t      metadata;
      const char*  value;
    };

    _OrthancPluginDatabaseAnswerType            answerType_;
    std::list<std::string>                      stringsStore_;

    std::vector<OrthancPluginAttachment>        attachments_;
    std::vector<OrthancPluginChange>            changes_;
    std::vector<OrthancPluginDicomTag>          tags_;
    std::vector<OrthancPluginExportedResource>  exported_;
    std::vector<OrthancPluginDatabaseEvent>     events_;
    std::vector<int32_t>                        integers32_;
    std::vector<int64_t>                        integers64_;
    std::vector<OrthancPluginMatchingResource>  matches_;
    std::vector<Metadata>                       metadata_;
    std::vector<std::string>                    stringAnswers_;

    const char* StoreString(const std::string& s)
    {
      stringsStore_.push_back(s);
      return stringsStore_.back().c_str();
    }

    void SetupAnswerType(_OrthancPluginDatabaseAnswerType type)
    {
      if (answerType_ == _OrthancPluginDatabaseAnswerType_None)
      {
        answerType_ = type;
      }
      else if (answerType_ != type)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }

  public:
    Output() :
      answerType_(_OrthancPluginDatabaseAnswerType_None)
    {
    }

    void Clear()
    {
      // We don't systematically clear all the vectors, in order to
      // avoid spending unnecessary time

      switch (answerType_)
      {
        case _OrthancPluginDatabaseAnswerType_None:
          break;

        case _OrthancPluginDatabaseAnswerType_Attachment:
          attachments_.clear();
          break;

        case _OrthancPluginDatabaseAnswerType_Change:
          changes_.clear();
          break;

        case _OrthancPluginDatabaseAnswerType_DicomTag:
          tags_.clear();
          break;

        case _OrthancPluginDatabaseAnswerType_ExportedResource:
          exported_.clear();
          break;

        case _OrthancPluginDatabaseAnswerType_Int32:
          integers32_.clear();
          break;

        case _OrthancPluginDatabaseAnswerType_Int64:
          integers64_.clear();
          break;

        case _OrthancPluginDatabaseAnswerType_MatchingResource:
          matches_.clear();
          break;

        case _OrthancPluginDatabaseAnswerType_Metadata:
          metadata_.clear();
          break;

        case _OrthancPluginDatabaseAnswerType_String:
          stringAnswers_.clear();
          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }

      answerType_ = _OrthancPluginDatabaseAnswerType_None;
      stringsStore_.clear();
      events_.clear();

      assert(attachments_.empty());
      assert(changes_.empty());
      assert(tags_.empty());
      assert(exported_.empty());
      assert(events_.empty());
      assert(integers32_.empty());
      assert(integers64_.empty());
      assert(matches_.empty());
      assert(metadata_.empty());
      assert(stringAnswers_.empty());
    }


    OrthancPluginErrorCode ReadAnswersCount(uint32_t& target) const
    {
      switch (answerType_)
      {
        case _OrthancPluginDatabaseAnswerType_None:
          target = static_cast<uint32_t>(0);
          break;

        case _OrthancPluginDatabaseAnswerType_Attachment:
          target = static_cast<uint32_t>(attachments_.size());
          break;

        case _OrthancPluginDatabaseAnswerType_Change:
          target = static_cast<uint32_t>(changes_.size());
          break;

        case _OrthancPluginDatabaseAnswerType_DicomTag:
          target = static_cast<uint32_t>(tags_.size());
          break;

        case _OrthancPluginDatabaseAnswerType_ExportedResource:
          target = static_cast<uint32_t>(exported_.size());
          break;

        case _OrthancPluginDatabaseAnswerType_Int32:
          target = static_cast<uint32_t>(integers32_.size());
          break;

        case _OrthancPluginDatabaseAnswerType_Int64:
          target = static_cast<uint32_t>(integers64_.size());
          break;

        case _OrthancPluginDatabaseAnswerType_MatchingResource:
          target = static_cast<uint32_t>(matches_.size());
          break;

        case _OrthancPluginDatabaseAnswerType_Metadata:
          target = static_cast<uint32_t>(metadata_.size());
          break;

        case _OrthancPluginDatabaseAnswerType_String:
          target = static_cast<uint32_t>(stringAnswers_.size());
          break;

        default:
          return OrthancPluginErrorCode_InternalError;
      }

      return OrthancPluginErrorCode_Success;
    }


    OrthancPluginErrorCode ReadAnswerAttachment(OrthancPluginAttachment& target /* out */,
                                                uint32_t index) const
    {
      if (index < attachments_.size())
      {
        target = attachments_[index];
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadAnswerChange(OrthancPluginChange& target /* out */,
                                            uint32_t index) const
    {
      if (index < changes_.size())
      {
        target = changes_[index];
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadAnswerDicomTag(uint16_t& group,
                                              uint16_t& element,
                                              const char*& value,
                                              uint32_t index) const
    {
      if (index < tags_.size())
      {
        const OrthancPluginDicomTag& tag = tags_[index];
        group = tag.group;
        element = tag.element;
        value = tag.value;
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadAnswerExportedResource(OrthancPluginExportedResource& target /* out */,
                                                      uint32_t index) const
    {
      if (index < exported_.size())
      {
        target = exported_[index];
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadAnswerInt32(int32_t& target,
                                           uint32_t index) const
    {
      if (index < integers32_.size())
      {
        target = integers32_[index];
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadAnswerInt64(int64_t& target,
                                           uint32_t index) const
    {
      if (index < integers64_.size())
      {
        target = integers64_[index];
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadAnswerMatchingResource(OrthancPluginMatchingResource& target,
                                                      uint32_t index) const
    {
      if (index < matches_.size())
      {
        target = matches_[index];
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadAnswerMetadata(int32_t& metadata,
                                              const char*& value,
                                              uint32_t index) const
    {
      if (index < metadata_.size())
      {
        const Metadata& tmp = metadata_[index];
        metadata = tmp.metadata;
        value = tmp.value;
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadAnswerString(const char*& target,
                                            uint32_t index) const
    {
      if (index < stringAnswers_.size())
      {
        target = stringAnswers_[index].c_str();
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    OrthancPluginErrorCode ReadEventsCount(uint32_t& target /* out */) const
    {
      target = static_cast<uint32_t>(events_.size());
      return OrthancPluginErrorCode_Success;
    }


    OrthancPluginErrorCode ReadEvent(OrthancPluginDatabaseEvent& event /* out */,
                                     uint32_t index) const
    {
      if (index < events_.size())
      {
        event = events_[index];
        return OrthancPluginErrorCode_Success;
      }
      else
      {
        return OrthancPluginErrorCode_ParameterOutOfRange;
      }
    }


    virtual void SignalDeletedAttachment(const std::string& uuid,
                                         int32_t            contentType,
                                         uint64_t           uncompressedSize,
                                         const std::string& uncompressedHash,
                                         int32_t            compressionType,
                                         uint64_t           compressedSize,
                                         const std::string& compressedHash) ORTHANC_OVERRIDE
    {
      OrthancPluginDatabaseEvent event;
      event.type = OrthancPluginDatabaseEventType_DeletedAttachment;
      event.content.attachment.uuid = StoreString(uuid);
      event.content.attachment.contentType = contentType;
      event.content.attachment.uncompressedSize = uncompressedSize;
      event.content.attachment.uncompressedHash = StoreString(uncompressedHash);
      event.content.attachment.compressionType = compressionType;
      event.content.attachment.compressedSize = compressedSize;
      event.content.attachment.compressedHash = StoreString(compressedHash);

      events_.push_back(event);
    }


    virtual void SignalDeletedResource(const std::string& publicId,
                                       OrthancPluginResourceType resourceType) ORTHANC_OVERRIDE
    {
      OrthancPluginDatabaseEvent event;
      event.type = OrthancPluginDatabaseEventType_DeletedResource;
      event.content.resource.level = resourceType;
      event.content.resource.publicId = StoreString(publicId);

      events_.push_back(event);
    }


    virtual void SignalRemainingAncestor(const std::string& ancestorId,
                                         OrthancPluginResourceType ancestorType) ORTHANC_OVERRIDE
    {
      OrthancPluginDatabaseEvent event;
      event.type = OrthancPluginDatabaseEventType_RemainingAncestor;
      event.content.resource.level = ancestorType;
      event.content.resource.publicId = StoreString(ancestorId);

      events_.push_back(event);
    }


    virtual void AnswerAttachment(const std::string& uuid,
                                  int32_t            contentType,
                                  uint64_t           uncompressedSize,
                                  const std::string& uncompressedHash,
                                  int32_t            compressionType,
                                  uint64_t           compressedSize,
                                  const std::string& compressedHash) ORTHANC_OVERRIDE
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_Attachment);

      OrthancPluginAttachment attachment;
      attachment.uuid = StoreString(uuid);
      attachment.contentType = contentType;
      attachment.uncompressedSize = uncompressedSize;
      attachment.uncompressedHash = StoreString(uncompressedHash);
      attachment.compressionType = compressionType;
      attachment.compressedSize = compressedSize;
      attachment.compressedHash = StoreString(compressedHash);

      attachments_.push_back(attachment);
    }


    virtual void AnswerChange(int64_t                    seq,
                              int32_t                    changeType,
                              OrthancPluginResourceType  resourceType,
                              const std::string&         publicId,
                              const std::string&         date) ORTHANC_OVERRIDE
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_Change);

      OrthancPluginChange change;
      change.seq = seq;
      change.changeType = changeType;
      change.resourceType = resourceType;
      change.publicId = StoreString(publicId);
      change.date = StoreString(date);

      changes_.push_back(change);
    }


    virtual void AnswerDicomTag(uint16_t group,
                                uint16_t element,
                                const std::string& value) ORTHANC_OVERRIDE
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_DicomTag);

      OrthancPluginDicomTag tag;
      tag.group = group;
      tag.element = element;
      tag.value = StoreString(value);

      tags_.push_back(tag);
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
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_ExportedResource);

      OrthancPluginExportedResource exported;
      exported.seq = seq;
      exported.resourceType = resourceType;
      exported.publicId = StoreString(publicId);
      exported.modality = StoreString(modality);
      exported.date = StoreString(date);
      exported.patientId = StoreString(patientId);
      exported.studyInstanceUid = StoreString(studyInstanceUid);
      exported.seriesInstanceUid = StoreString(seriesInstanceUid);
      exported.sopInstanceUid = StoreString(sopInstanceUid);

      exported_.push_back(exported);
    }


    virtual void AnswerMatchingResource(const std::string& resourceId) ORTHANC_OVERRIDE
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_MatchingResource);

      OrthancPluginMatchingResource match;
      match.resourceId = StoreString(resourceId);
      match.someInstanceId = NULL;

      matches_.push_back(match);
    }


    virtual void AnswerMatchingResource(const std::string& resourceId,
                                        const std::string& someInstanceId) ORTHANC_OVERRIDE
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_MatchingResource);

      OrthancPluginMatchingResource match;
      match.resourceId = StoreString(resourceId);
      match.someInstanceId = StoreString(someInstanceId);

      matches_.push_back(match);
    }


    void AnswerIntegers32(const std::list<int32_t>& values)
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_Int32);
      CopyListToVector(integers32_, values);
    }


    void AnswerIntegers64(const std::list<int64_t>& values)
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_Int64);
      CopyListToVector(integers64_, values);
    }


    void AnswerInteger64(int64_t value)
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_Int64);

      integers64_.resize(1);
      integers64_[0] = value;
    }


    void AnswerMetadata(int32_t metadata,
                        const std::string& value)
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_Metadata);

      Metadata tmp;
      tmp.metadata = metadata;
      tmp.value = StoreString(value);

      metadata_.push_back(tmp);
    }


    void AnswerStrings(const std::list<std::string>& values)
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_String);
      CopyListToVector(stringAnswers_, values);
    }


    void AnswerString(const std::string& value)
    {
      SetupAnswerType(_OrthancPluginDatabaseAnswerType_String);

      if (stringAnswers_.empty())
      {
        stringAnswers_.push_back(value);
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
    }
  };


  IDatabaseBackendOutput* DatabaseBackendAdapterV3::Factory::CreateOutput()
  {
    return new DatabaseBackendAdapterV3::Output;
  }


  class DatabaseBackendAdapterV3::Transaction : public boost::noncopyable
  {
  private:
    Adapter&   adapter_;
    std::unique_ptr<Adapter::DatabaseAccessor>  accessor_;
    std::unique_ptr<Output>    output_;

  public:
    Transaction(Adapter& adapter) :
      adapter_(adapter),
      accessor_(new Adapter::DatabaseAccessor(adapter)),
      output_(new Output)
    {
    }

    ~Transaction()
    {
    }

    IndexBackend& GetBackend() const
    {
      return accessor_->GetBackend();
    }

    Output& GetOutput() const
    {
      return *output_;
    }

    DatabaseManager& GetManager() const
    {
      return accessor_->GetManager();
    }
  };


  static OrthancPluginErrorCode ReadAnswersCount(OrthancPluginDatabaseTransaction* transaction,
                                                 uint32_t* target /* out */)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswersCount(*target);
  }


  static OrthancPluginErrorCode ReadAnswerAttachment(OrthancPluginDatabaseTransaction* transaction,
                                                     OrthancPluginAttachment* target /* out */,
                                                     uint32_t index)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerAttachment(*target, index);
  }


  static OrthancPluginErrorCode ReadAnswerChange(OrthancPluginDatabaseTransaction* transaction,
                                                 OrthancPluginChange* target /* out */,
                                                 uint32_t index)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerChange(*target, index);
  }


  static OrthancPluginErrorCode ReadAnswerDicomTag(OrthancPluginDatabaseTransaction* transaction,
                                                   uint16_t* group,
                                                   uint16_t* element,
                                                   const char** value,
                                                   uint32_t index)
  {
    assert(group != NULL);
    assert(element != NULL);
    assert(value != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerDicomTag(*group, *element, *value, index);
  }


  static OrthancPluginErrorCode ReadAnswerExportedResource(OrthancPluginDatabaseTransaction* transaction,
                                                           OrthancPluginExportedResource* target /* out */,
                                                           uint32_t index)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerExportedResource(*target, index);
  }


  static OrthancPluginErrorCode ReadAnswerInt32(OrthancPluginDatabaseTransaction* transaction,
                                                int32_t* target,
                                                uint32_t index)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerInt32(*target, index);
  }


  static OrthancPluginErrorCode ReadAnswerInt64(OrthancPluginDatabaseTransaction* transaction,
                                                int64_t* target,
                                                uint32_t index)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerInt64(*target, index);
  }


  static OrthancPluginErrorCode ReadAnswerMatchingResource(OrthancPluginDatabaseTransaction* transaction,
                                                           OrthancPluginMatchingResource* target,
                                                           uint32_t index)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerMatchingResource(*target, index);
  }


  static OrthancPluginErrorCode ReadAnswerMetadata(OrthancPluginDatabaseTransaction* transaction,
                                                   int32_t* metadata,
                                                   const char** value,
                                                   uint32_t index)
  {
    assert(metadata != NULL);
    assert(value != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerMetadata(*metadata, *value, index);
  }


  static OrthancPluginErrorCode ReadAnswerString(OrthancPluginDatabaseTransaction* transaction,
                                                 const char** target,
                                                 uint32_t index)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadAnswerString(*target, index);
  }


  static OrthancPluginErrorCode ReadEventsCount(OrthancPluginDatabaseTransaction* transaction,
                                                uint32_t* target /* out */)
  {
    assert(target != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadEventsCount(*target);
  }


  static OrthancPluginErrorCode ReadEvent(OrthancPluginDatabaseTransaction* transaction,
                                          OrthancPluginDatabaseEvent* event /* out */,
                                          uint32_t index)
  {
    assert(event != NULL);
    const DatabaseBackendAdapterV3::Transaction& that = *reinterpret_cast<const DatabaseBackendAdapterV3::Transaction*>(transaction);
    return that.GetOutput().ReadEvent(*event, index);
  }


  static OrthancPluginErrorCode Open(void* database)
  {
    DatabaseBackendAdapterV3::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV3::Adapter*>(database);

    try
    {
      adapter->OpenConnections();
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(adapter->GetContext());
  }


  static OrthancPluginErrorCode Close(void* database)
  {
    DatabaseBackendAdapterV3::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV3::Adapter*>(database);

    try
    {
      adapter->CloseConnections();
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(adapter->GetContext());
  }


  static OrthancPluginErrorCode DestructDatabase(void* database)
  {
    DatabaseBackendAdapterV3::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV3::Adapter*>(database);

    if (adapter == NULL)
    {
      return OrthancPluginErrorCode_InternalError;
    }
    else
    {
      if (isBackendInUse_)
      {
        isBackendInUse_ = false;
      }
      else
      {
        OrthancPluginLogError(adapter->GetContext(), "More than one index backend was registered, internal error");
      }

      delete adapter;

      return OrthancPluginErrorCode_Success;
    }
  }


  static OrthancPluginErrorCode GetDatabaseVersion(void* database,
                                                   uint32_t* version)
  {
    DatabaseBackendAdapterV3::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV3::Adapter*>(database);

    try
    {
      DatabaseBackendAdapterV3::Adapter::DatabaseAccessor accessor(*adapter);
      *version = accessor.GetBackend().GetDatabaseVersion(accessor.GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(adapter->GetContext());
  }


  static OrthancPluginErrorCode UpgradeDatabase(void* database,
                                                OrthancPluginStorageArea* storageArea,
                                                uint32_t  targetVersion)
  {
    DatabaseBackendAdapterV3::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV3::Adapter*>(database);

    try
    {
      DatabaseBackendAdapterV3::Adapter::DatabaseAccessor accessor(*adapter);
      accessor.GetBackend().UpgradeDatabase(accessor.GetManager(), targetVersion, storageArea);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(adapter->GetContext());
  }


  static OrthancPluginErrorCode HasRevisionsSupport(void* database,
                                                    uint8_t* target)
  {
    DatabaseBackendAdapterV3::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV3::Adapter*>(database);

    try
    {
      DatabaseBackendAdapterV3::Adapter::DatabaseAccessor accessor(*adapter);
      *target = (accessor.GetBackend().HasRevisionsSupport() ? 1 : 0);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(adapter->GetContext());
  }


  static OrthancPluginErrorCode StartTransaction(void* database,
                                                 OrthancPluginDatabaseTransaction** target /* out */,
                                                 OrthancPluginDatabaseTransactionType type)
  {
    DatabaseBackendAdapterV3::Adapter* adapter = reinterpret_cast<DatabaseBackendAdapterV3::Adapter*>(database);

    try
    {
      std::unique_ptr<DatabaseBackendAdapterV3::Transaction> transaction(new DatabaseBackendAdapterV3::Transaction(*adapter));

      switch (type)
      {
        case OrthancPluginDatabaseTransactionType_ReadOnly:
          transaction->GetManager().StartTransaction(TransactionType_ReadOnly);
          break;

        case OrthancPluginDatabaseTransactionType_ReadWrite:
          transaction->GetManager().StartTransaction(TransactionType_ReadWrite);
          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }

      *target = reinterpret_cast<OrthancPluginDatabaseTransaction*>(transaction.release());

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(adapter->GetContext());
  }


  static OrthancPluginErrorCode DestructTransaction(OrthancPluginDatabaseTransaction* transaction)
  {
    if (transaction == NULL)
    {
      return OrthancPluginErrorCode_NullPointer;
    }
    else
    {
      delete reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);
      return OrthancPluginErrorCode_Success;
    }
  }


  static OrthancPluginErrorCode Rollback(OrthancPluginDatabaseTransaction* transaction)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetManager().RollbackTransaction();
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode Commit(OrthancPluginDatabaseTransaction* transaction,
                                       int64_t fileSizeDelta /* TODO - not used? */)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetManager().CommitTransaction();
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode AddAttachment(OrthancPluginDatabaseTransaction* transaction,
                                              int64_t id,
                                              const OrthancPluginAttachment* attachment,
                                              int64_t revision)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().AddAttachment(t->GetManager(), id, *attachment, revision);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode ClearChanges(OrthancPluginDatabaseTransaction* transaction)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().ClearChanges(t->GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode ClearExportedResources(OrthancPluginDatabaseTransaction* transaction)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().ClearExportedResources(t->GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode ClearMainDicomTags(OrthancPluginDatabaseTransaction* transaction,
                                                   int64_t resourceId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().ClearMainDicomTags(t->GetManager(), resourceId);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode CreateInstance(OrthancPluginDatabaseTransaction* transaction,
                                               OrthancPluginCreateInstanceResult* target /* out */,
                                               const char* hashPatient,
                                               const char* hashStudy,
                                               const char* hashSeries,
                                               const char* hashInstance)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      if (t->GetBackend().HasCreateInstance())
      {
        t->GetBackend().CreateInstance(*target, t->GetManager(), hashPatient, hashStudy, hashSeries, hashInstance);
      }
      else
      {
        t->GetBackend().CreateInstanceGeneric(*target, t->GetManager(), hashPatient, hashStudy, hashSeries, hashInstance);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode DeleteAttachment(OrthancPluginDatabaseTransaction* transaction,
                                                 int64_t id,
                                                 int32_t contentType)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().DeleteAttachment(t->GetOutput(), t->GetManager(), id, contentType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode DeleteMetadata(OrthancPluginDatabaseTransaction* transaction,
                                               int64_t id,
                                               int32_t metadataType)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().DeleteMetadata(t->GetManager(), id, metadataType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode DeleteResource(OrthancPluginDatabaseTransaction* transaction,
                                               int64_t id)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().DeleteResource(t->GetOutput(), t->GetManager(), id);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetAllMetadata(OrthancPluginDatabaseTransaction* transaction,
                                               int64_t id)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::map<int32_t, std::string> values;
      t->GetBackend().GetAllMetadata(values, t->GetManager(), id);

      for (std::map<int32_t, std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
      {
        t->GetOutput().AnswerMetadata(it->first, it->second);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetAllPublicIds(OrthancPluginDatabaseTransaction* transaction,
                                                OrthancPluginResourceType resourceType)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::list<std::string> values;
      t->GetBackend().GetAllPublicIds(values, t->GetManager(), resourceType);
      t->GetOutput().AnswerStrings(values);

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetAllPublicIdsWithLimit(OrthancPluginDatabaseTransaction* transaction,
                                                         OrthancPluginResourceType resourceType,
                                                         uint64_t since,
                                                         uint64_t limit)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::list<std::string> values;
      t->GetBackend().GetAllPublicIds(values, t->GetManager(), resourceType, since, limit);
      t->GetOutput().AnswerStrings(values);

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetChanges(OrthancPluginDatabaseTransaction* transaction,
                                           uint8_t* targetDone /* out */,
                                           int64_t since,
                                           uint32_t maxResults)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      bool done;
      t->GetBackend().GetChanges(t->GetOutput(), done, t->GetManager(), since, maxResults);
      *targetDone = (done ? 1 : 0);

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetChildrenInternalId(OrthancPluginDatabaseTransaction* transaction,
                                                      int64_t id)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::list<int64_t> values;
      t->GetBackend().GetChildrenInternalId(values, t->GetManager(), id);
      t->GetOutput().AnswerIntegers64(values);

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetChildrenMetadata(OrthancPluginDatabaseTransaction* transaction,
                                                    int64_t resourceId,
                                                    int32_t metadata)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::list<std::string> values;
      t->GetBackend().GetChildrenMetadata(values, t->GetManager(), resourceId, metadata);
      t->GetOutput().AnswerStrings(values);

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetChildrenPublicId(OrthancPluginDatabaseTransaction* transaction,
                                                    int64_t id)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::list<std::string> values;
      t->GetBackend().GetChildrenPublicId(values, t->GetManager(), id);
      t->GetOutput().AnswerStrings(values);

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetExportedResources(OrthancPluginDatabaseTransaction* transaction,
                                                     uint8_t* targetDone /* out */,
                                                     int64_t since,
                                                     uint32_t maxResults)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      bool done;
      t->GetBackend().GetExportedResources(t->GetOutput(), done, t->GetManager(), since, maxResults);
      *targetDone = (done ? 1 : 0);

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetLastChange(OrthancPluginDatabaseTransaction* transaction)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().GetLastChange(t->GetOutput(), t->GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetLastChangeIndex(OrthancPluginDatabaseTransaction* transaction,
                                                   int64_t* target)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      *target = t->GetBackend().GetLastChangeIndex(t->GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetLastExportedResource(OrthancPluginDatabaseTransaction* transaction)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().GetLastExportedResource(t->GetOutput(), t->GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetMainDicomTags(OrthancPluginDatabaseTransaction* transaction,
                                                 int64_t id)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().GetMainDicomTags(t->GetOutput(), t->GetManager(), id);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetPublicId(OrthancPluginDatabaseTransaction* transaction,
                                            int64_t id)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetOutput().AnswerString(t->GetBackend().GetPublicId(t->GetManager(), id));
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetResourcesCount(OrthancPluginDatabaseTransaction* transaction,
                                                  uint64_t* target /* out */,
                                                  OrthancPluginResourceType resourceType)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      *target = t->GetBackend().GetResourcesCount(t->GetManager(), resourceType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetResourceType(OrthancPluginDatabaseTransaction* transaction,
                                                OrthancPluginResourceType* target /* out */,
                                                uint64_t resourceId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      *target = t->GetBackend().GetResourceType(t->GetManager(), resourceId);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetTotalCompressedSize(OrthancPluginDatabaseTransaction* transaction,
                                                       uint64_t* target /* out */)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      *target = t->GetBackend().GetTotalCompressedSize(t->GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode GetTotalUncompressedSize(OrthancPluginDatabaseTransaction* transaction,
                                                         uint64_t* target /* out */)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      *target = t->GetBackend().GetTotalUncompressedSize(t->GetManager());
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode IsDiskSizeAbove(OrthancPluginDatabaseTransaction* transaction,
                                                uint8_t* target,
                                                uint64_t threshold)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      bool above = (t->GetBackend().GetTotalCompressedSize(t->GetManager()) >= threshold);
      *target = (above ? 1 : 0);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode IsExistingResource(OrthancPluginDatabaseTransaction* transaction,
                                                   uint8_t* target,
                                                   int64_t resourceId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      bool exists = t->GetBackend().IsExistingResource(t->GetManager(), resourceId);
      *target = (exists ? 1 : 0);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode IsProtectedPatient(OrthancPluginDatabaseTransaction* transaction,
                                                   uint8_t* target,
                                                   int64_t resourceId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      bool isProtected = t->GetBackend().IsProtectedPatient(t->GetManager(), resourceId);
      *target = (isProtected ? 1 : 0);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode ListAvailableAttachments(OrthancPluginDatabaseTransaction* transaction,
                                                         int64_t resourceId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::list<int32_t> values;
      t->GetBackend().ListAvailableAttachments(values, t->GetManager(), resourceId);
      t->GetOutput().AnswerIntegers32(values);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LogChange(OrthancPluginDatabaseTransaction* transaction,
                                          int32_t changeType,
                                          int64_t resourceId,
                                          OrthancPluginResourceType resourceType,
                                          const char* date)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().LogChange(t->GetManager(), changeType, resourceId, resourceType, date);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LogExportedResource(OrthancPluginDatabaseTransaction* transaction,
                                                    OrthancPluginResourceType resourceType,
                                                    const char* publicId,
                                                    const char* modality,
                                                    const char* date,
                                                    const char* patientId,
                                                    const char* studyInstanceUid,
                                                    const char* seriesInstanceUid,
                                                    const char* sopInstanceUid)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      OrthancPluginExportedResource exported;
      exported.seq = 0;
      exported.resourceType = resourceType;
      exported.publicId = publicId;
      exported.modality = modality;
      exported.date = date;
      exported.patientId = patientId;
      exported.studyInstanceUid = studyInstanceUid;
      exported.seriesInstanceUid = seriesInstanceUid;
      exported.sopInstanceUid = sopInstanceUid;

      t->GetOutput().Clear();
      t->GetBackend().LogExportedResource(t->GetManager(), exported);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LookupAttachment(OrthancPluginDatabaseTransaction* transaction,
                                                 int64_t* revision /* out */,
                                                 int64_t resourceId,
                                                 int32_t contentType)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().LookupAttachment(t->GetOutput(), *revision, t->GetManager(), resourceId, contentType);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LookupGlobalProperty(OrthancPluginDatabaseTransaction* transaction,
                                                     const char* serverIdentifier,
                                                     int32_t property)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::string s;
      if (t->GetBackend().LookupGlobalProperty(s, t->GetManager(), serverIdentifier, property))
      {
        t->GetOutput().AnswerString(s);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LookupMetadata(OrthancPluginDatabaseTransaction* transaction,
                                               int64_t* revision /* out */,
                                               int64_t id,
                                               int32_t metadata)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::string s;
      if (t->GetBackend().LookupMetadata(s, *revision, t->GetManager(), id, metadata))
      {
        t->GetOutput().AnswerString(s);
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LookupParent(OrthancPluginDatabaseTransaction* transaction,
                                             uint8_t* existing /* out */,
                                             int64_t* parentId /* out */,
                                             int64_t id)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      if (t->GetBackend().LookupParent(*parentId, t->GetManager(), id))
      {
        *existing = 1;
      }
      else
      {
        *existing = 0;
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LookupResource(OrthancPluginDatabaseTransaction* transaction,
                                               uint8_t* isExisting /* out */,
                                               int64_t* id /* out */,
                                               OrthancPluginResourceType* type /* out */,
                                               const char* publicId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      if (t->GetBackend().LookupResource(*id, *type, t->GetManager(), publicId))
      {
        *isExisting = 1;
      }
      else
      {
        *isExisting = 0;
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LookupResources(OrthancPluginDatabaseTransaction* transaction,
                                                uint32_t constraintsCount,
                                                const OrthancPluginDatabaseConstraint* constraints,
                                                OrthancPluginResourceType queryLevel,
                                                uint32_t limit,
                                                uint8_t requestSomeInstanceId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::vector<Orthanc::DatabaseConstraint> lookup;
      lookup.reserve(constraintsCount);

      for (uint32_t i = 0; i < constraintsCount; i++)
      {
        lookup.push_back(Orthanc::DatabaseConstraint(constraints[i]));
      }

      t->GetBackend().LookupResources(t->GetOutput(), t->GetManager(), lookup, queryLevel, limit, (requestSomeInstanceId != 0));
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode LookupResourceAndParent(OrthancPluginDatabaseTransaction* transaction,
                                                        uint8_t* isExisting /* out */,
                                                        int64_t* id /* out */,
                                                        OrthancPluginResourceType* type /* out */,
                                                        const char* publicId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      std::string parent;
      if (t->GetBackend().LookupResourceAndParent(*id, *type, parent, t->GetManager(), publicId))
      {
        *isExisting = 1;

        if (!parent.empty())
        {
          t->GetOutput().AnswerString(parent);
        }
      }
      else
      {
        *isExisting = 0;
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode SelectPatientToRecycle(OrthancPluginDatabaseTransaction* transaction,
                                                       uint8_t* patientAvailable,
                                                       int64_t* patientId)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      if (t->GetBackend().SelectPatientToRecycle(*patientId, t->GetManager()))
      {
        *patientAvailable = 1;
      }
      else
      {
        *patientAvailable = 0;
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode SelectPatientToRecycle2(OrthancPluginDatabaseTransaction* transaction,
                                                        uint8_t* patientAvailable,
                                                        int64_t* patientId,
                                                        int64_t patientIdToAvoid)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();

      if (t->GetBackend().SelectPatientToRecycle(*patientId, t->GetManager(), patientIdToAvoid))
      {
        *patientAvailable = 1;
      }
      else
      {
        *patientAvailable = 0;
      }

      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode SetGlobalProperty(OrthancPluginDatabaseTransaction* transaction,
                                                  const char* serverIdentifier,
                                                  int32_t property,
                                                  const char* value)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().SetGlobalProperty(t->GetManager(), serverIdentifier, property, value);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode SetMetadata(OrthancPluginDatabaseTransaction* transaction,
                                            int64_t id,
                                            int32_t metadata,
                                            const char* value,
                                            int64_t revision)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().SetMetadata(t->GetManager(), id, metadata, value, revision);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode SetProtectedPatient(OrthancPluginDatabaseTransaction* transaction,
                                                    int64_t id,
                                                    uint8_t isProtected)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().SetProtectedPatient(t->GetManager(), id, (isProtected != 0));
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  static OrthancPluginErrorCode SetResourcesContent(OrthancPluginDatabaseTransaction* transaction,
                                                    uint32_t countIdentifierTags,
                                                    const OrthancPluginResourcesContentTags* identifierTags,
                                                    uint32_t countMainDicomTags,
                                                    const OrthancPluginResourcesContentTags* mainDicomTags,
                                                    uint32_t countMetadata,
                                                    const OrthancPluginResourcesContentMetadata* metadata)
  {
    DatabaseBackendAdapterV3::Transaction* t = reinterpret_cast<DatabaseBackendAdapterV3::Transaction*>(transaction);

    try
    {
      t->GetOutput().Clear();
      t->GetBackend().SetResourcesContent(t->GetManager(), countIdentifierTags, identifierTags,
                                          countMainDicomTags, mainDicomTags, countMetadata, metadata);
      return OrthancPluginErrorCode_Success;
    }
    ORTHANC_PLUGINS_DATABASE_CATCH(t->GetBackend().GetContext());
  }


  void DatabaseBackendAdapterV3::Register(IndexBackend* backend,
                                          size_t countConnections,
                                          unsigned int maxDatabaseRetries)
  {
    if (isBackendInUse_)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }

    if (backend == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    OrthancPluginDatabaseBackendV3 params;
    memset(&params, 0, sizeof(params));

    params.readAnswersCount = ReadAnswersCount;
    params.readAnswerAttachment = ReadAnswerAttachment;
    params.readAnswerChange = ReadAnswerChange;
    params.readAnswerDicomTag = ReadAnswerDicomTag;
    params.readAnswerExportedResource = ReadAnswerExportedResource;
    params.readAnswerInt32 = ReadAnswerInt32;
    params.readAnswerInt64 = ReadAnswerInt64;
    params.readAnswerMatchingResource = ReadAnswerMatchingResource;
    params.readAnswerMetadata = ReadAnswerMetadata;
    params.readAnswerString = ReadAnswerString;

    params.readEventsCount = ReadEventsCount;
    params.readEvent = ReadEvent;

    params.open = Open;
    params.close = Close;
    params.destructDatabase = DestructDatabase;
    params.getDatabaseVersion = GetDatabaseVersion;
    params.upgradeDatabase = UpgradeDatabase;
    params.hasRevisionsSupport = HasRevisionsSupport;
    params.startTransaction = StartTransaction;
    params.destructTransaction = DestructTransaction;
    params.rollback = Rollback;
    params.commit = Commit;

    params.addAttachment = AddAttachment;
    params.clearChanges = ClearChanges;
    params.clearExportedResources = ClearExportedResources;
    params.clearMainDicomTags = ClearMainDicomTags;
    params.createInstance = CreateInstance;
    params.deleteAttachment = DeleteAttachment;
    params.deleteMetadata = DeleteMetadata;
    params.deleteResource = DeleteResource;
    params.getAllMetadata = GetAllMetadata;
    params.getAllPublicIds = GetAllPublicIds;
    params.getAllPublicIdsWithLimit = GetAllPublicIdsWithLimit;
    params.getChanges = GetChanges;
    params.getChildrenInternalId = GetChildrenInternalId;
    params.getChildrenMetadata = GetChildrenMetadata;
    params.getChildrenPublicId = GetChildrenPublicId;
    params.getExportedResources = GetExportedResources;
    params.getLastChange = GetLastChange;
    params.getLastChangeIndex = GetLastChangeIndex;
    params.getLastExportedResource = GetLastExportedResource;
    params.getMainDicomTags = GetMainDicomTags;
    params.getPublicId = GetPublicId;
    params.getResourceType = GetResourceType;
    params.getResourcesCount = GetResourcesCount;
    params.getTotalCompressedSize = GetTotalCompressedSize;
    params.getTotalUncompressedSize = GetTotalUncompressedSize;
    params.isDiskSizeAbove = IsDiskSizeAbove;
    params.isExistingResource = IsExistingResource;
    params.isProtectedPatient = IsProtectedPatient;
    params.listAvailableAttachments = ListAvailableAttachments;
    params.logChange = LogChange;
    params.logExportedResource = LogExportedResource;
    params.lookupAttachment = LookupAttachment;
    params.lookupGlobalProperty = LookupGlobalProperty;
    params.lookupMetadata = LookupMetadata;
    params.lookupParent = LookupParent;
    params.lookupResource = LookupResource;
    params.lookupResourceAndParent = LookupResourceAndParent;
    params.lookupResources = LookupResources;
    params.selectPatientToRecycle = SelectPatientToRecycle;
    params.selectPatientToRecycle2 = SelectPatientToRecycle2;
    params.setGlobalProperty = SetGlobalProperty;
    params.setMetadata = SetMetadata;
    params.setProtectedPatient = SetProtectedPatient;
    params.setResourcesContent = SetResourcesContent;

    OrthancPluginContext* context = backend->GetContext();

    if (OrthancPluginRegisterDatabaseBackendV3(
          context, &params, sizeof(params), maxDatabaseRetries,
          new Adapter(backend, countConnections)) != OrthancPluginErrorCode_Success)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError, "Unable to register the database backend");
    }

    backend->SetOutputFactory(new Factory);

    isBackendInUse_ = true;
  }


  void DatabaseBackendAdapterV3::Finalize()
  {
    if (isBackendInUse_)
    {
      fprintf(stderr, "The Orthanc core has not destructed the index backend, internal error\n");
    }
  }
}

#  endif
#endif
