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


#include "StorageBackend.h"

#if HAS_ORTHANC_EXCEPTION != 1
#  error HAS_ORTHANC_EXCEPTION must be set to 1
#endif

// MongoDB: "BinaryStringValue.h" and "ResultFileValue.h" are only used by the SQL "AccessorBase"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>
#include "../../Resources/Orthanc/Plugins/OrthancPluginException.h"

#include <boost/thread.hpp>
#include <cassert>
#include <limits>


#define ORTHANC_PLUGINS_DATABASE_CATCH                                  \
  catch (::Orthanc::OrthancException& e)                                \
  {                                                                     \
    return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());       \
  }                                                                     \
  catch (::std::runtime_error& e)                                       \
  {                                                                     \
    std::string s = "Exception in storage area back-end: " + std::string(e.what()); \
    OrthancPluginLogError(context_, s.c_str());                         \
    return OrthancPluginErrorCode_DatabasePlugin;                       \
  }                                                                     \
  catch (...)                                                           \
  {                                                                     \
    OrthancPluginLogError(context_, "Native exception");                \
    return OrthancPluginErrorCode_DatabasePlugin;                       \
  }


namespace OrthancDatabases
{
  class StorageBackend::ReadWholeOperation : public StorageBackend::IDatabaseOperation
  {
  private:
    IFileContentVisitor&      visitor_;
    const char*               uuid_;
    OrthancPluginContentType  type_;

  public:
    ReadWholeOperation(IFileContentVisitor& visitor,
                       const char* uuid,
                       OrthancPluginContentType type) :
      visitor_(visitor),
      uuid_(uuid),
      type_(type)
    {
    }

    virtual void Execute(StorageBackend::IAccessor& accessor) ORTHANC_OVERRIDE
    {
      accessor.ReadWhole(visitor_, uuid_, type_);
    }
  };


  StorageBackend::StorageBackend(unsigned int maxRetries) :
    maxRetries_(maxRetries)
  {
  }


  // MongoDB: the SQL implementation of "AccessorBase" is removed, cf. "StorageBackend.h"

  static OrthancPluginContext* context_ = NULL;
  static std::unique_ptr<StorageBackend>  backend_;
    

  static OrthancPluginErrorCode StorageCreate(const char* uuid,
                                              const void* content,
                                              int64_t size,
                                              OrthancPluginContentType type)
  {
    class Operation : public StorageBackend::IDatabaseOperation
    {
    private:
      const char*               uuid_;
      const void*               content_;
      int64_t                   size_;
      OrthancPluginContentType  type_;
      
    public:
      Operation(const char* uuid,
                const void* content,
                int64_t size,
                OrthancPluginContentType type) :
        uuid_(uuid),
        content_(content),
        size_(size),
        type_(type)
      {
      }
      
      virtual void Execute(StorageBackend::IAccessor& accessor) ORTHANC_OVERRIDE
      {
        accessor.Create(uuid_, content_, size_, type_);
      }
    };


    try
    {
      if (backend_.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      else
      {
        Operation operation(uuid, content, size, type);
        backend_->Execute(operation);
        return OrthancPluginErrorCode_Success;
      }
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 9, 0)
  static OrthancPluginErrorCode StorageReadWhole(OrthancPluginMemoryBuffer64* target,
                                                 const char* uuid,
                                                 OrthancPluginContentType type)
  {
    class Visitor : public StorageBackend::IFileContentVisitor
    {
    private:
      OrthancPluginMemoryBuffer64* target_;
      bool                         success_;
      
    public:
      explicit Visitor(OrthancPluginMemoryBuffer64* target) :
        target_(target),
        success_(false)
      {
        if (target == NULL)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
        }
      }

      virtual bool IsSuccess() const ORTHANC_OVERRIDE
      {
        return success_;
      }
      
      virtual void Assign(const std::string& content) ORTHANC_OVERRIDE
      {
        if (success_)
        {
          ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
        }
        else
        {
          assert(context_ != NULL);
          
          if (OrthancPluginCreateMemoryBuffer64(context_, target_, static_cast<uint64_t>(content.size())) !=
              OrthancPluginErrorCode_Success)
          {
            throw Orthanc::OrthancException(Orthanc::ErrorCode_NotEnoughMemory);
          }

          if (!content.empty())
          {
            memcpy(target_->data, content.c_str(), content.size());
          }

          success_ = true;
        }
      }
    };


    try
    {
      if (backend_.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      else
      {
        Visitor visitor(target);

        {
          StorageBackend::ReadWholeOperation operation(visitor, uuid, type);
          backend_->Execute(operation);
        }

        return OrthancPluginErrorCode_Success;
      }
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }

  
  static OrthancPluginErrorCode StorageReadRange(OrthancPluginMemoryBuffer64* target,
                                                 const char* uuid,
                                                 OrthancPluginContentType type,
                                                 uint64_t start)
  {
    class Visitor : public StorageBackend::IFileContentVisitor
    {
    private:
      OrthancPluginMemoryBuffer64* target_;  // This buffer is already allocated by the Orthanc core
      bool                         success_;
      
    public:
      explicit Visitor(OrthancPluginMemoryBuffer64* target) :
        target_(target),
        success_(false)
      {
        if (target == NULL)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
        }
      }

      virtual bool IsSuccess() const ORTHANC_OVERRIDE
      {
        return success_;
      }
      
      virtual void Assign(const std::string& content) ORTHANC_OVERRIDE
      {
        if (success_)
        {
          ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
        }
        else
        {
          if (content.size() != target_->size)
          {
            ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
          }
          
          if (!content.empty())
          {
            memcpy(target_->data, content.c_str(), content.size());
          }

          success_ = true;
        }
      }
    };


    class Operation : public StorageBackend::IDatabaseOperation
    {
    private:
      Visitor&                  visitor_;
      const char*               uuid_;
      OrthancPluginContentType  type_;
      uint64_t                  start_;
      size_t                    length_;

    public:
      Operation(Visitor& visitor,
                const char* uuid,
                OrthancPluginContentType type,
                uint64_t start,
                size_t length) :
        visitor_(visitor),
        uuid_(uuid),
        type_(type),
        start_(start),
        length_(length)
      {
      }

      virtual void Execute(StorageBackend::IAccessor& accessor) ORTHANC_OVERRIDE
      {
        accessor.ReadRange(visitor_, uuid_, type_, start_, length_);
      }
    };


    try
    {
      if (backend_.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      else
      {
        Visitor visitor(target);

        {
          Operation operation(visitor, uuid, type, start, target->size);
          backend_->Execute(operation);
        }

        return OrthancPluginErrorCode_Success;
      }
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }
#  endif
#endif

  
  static OrthancPluginErrorCode StorageRead(void** data,
                                            int64_t* size,
                                            const char* uuid,
                                            OrthancPluginContentType type)
  {
    class Visitor : public StorageBackend::IFileContentVisitor
    {
    private:
      void**    data_;
      int64_t*  size_;
      bool      success_;
      
    public:
      Visitor(void** data,
              int64_t* size) :
        data_(data),
        size_(size),
        success_(false)
      {
      }

      virtual ~Visitor() ORTHANC_OVERRIDE
      {
        if (data_ != NULL /* this condition is invalidated by "Release()" */ &&
            *data_ != NULL)
        {
          free(*data_);
        }
      }

      void Release()
      {
        data_ = NULL;
      }      

      virtual bool IsSuccess() const ORTHANC_OVERRIDE
      {
        return success_;
      }
      
      virtual void Assign(const std::string& content) ORTHANC_OVERRIDE
      {
        if (success_)
        {
          ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
        }
        else if (data_ == NULL)
        {
          // "Release()" has been called
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        }
        else
        {
          if (content.empty())
          {
            *data_ = NULL;
            *size_ = 0;
          }
          else
          {
            *size_ = static_cast<int64_t>(content.size());
    
            if (static_cast<size_t>(*size_) != content.size())
            {
              throw Orthanc::OrthancException(Orthanc::ErrorCode_NotEnoughMemory,
                                              "File cannot be stored in a 63bit buffer");
            }

            *data_ = malloc(*size_);
            if (*data_ == NULL)
            {
              throw Orthanc::OrthancException(Orthanc::ErrorCode_NotEnoughMemory);
            }

            memcpy(*data_, content.c_str(), *size_);
          }
          
          success_ = true;
        }
      }
    };


    try
    {
      if (backend_.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      else if (data == NULL ||
               size == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
      }
      else
      {
        Visitor visitor(data, size);

        {
          StorageBackend::ReadWholeOperation operation(visitor, uuid, type);
          backend_->Execute(operation);
        }

        visitor.Release();

        return OrthancPluginErrorCode_Success;
      }
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }


  static OrthancPluginErrorCode StorageRemove(const char* uuid,
                                              OrthancPluginContentType type)
  {
    class Operation : public StorageBackend::IDatabaseOperation
    {
    private:
      const char*               uuid_;
      OrthancPluginContentType  type_;
      
    public:
      Operation(const char* uuid,
                OrthancPluginContentType type) :
        uuid_(uuid),
        type_(type)
      {
      }
      
      virtual void Execute(StorageBackend::IAccessor& accessor) ORTHANC_OVERRIDE
      {
        accessor.Remove(uuid_, type_);
      }
    };

    
    try
    {
      if (backend_.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }
      else
      {
        Operation operation(uuid, type);
        backend_->Execute(operation);
        return OrthancPluginErrorCode_Success;
      }
    }
    ORTHANC_PLUGINS_DATABASE_CATCH;
  }

  
  void StorageBackend::Register(OrthancPluginContext* context,
                                StorageBackend* backend)
  {
    if (context == NULL ||
        backend == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }
    else if (context_ != NULL ||
             backend_.get() != NULL)
    {
      // This function can only be invoked once in the plugin
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      context_ = context;
      backend_.reset(backend);

      bool hasLoadedV2 = false;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)         // Macro introduced in Orthanc 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 9, 0)
      if (OrthancPluginCheckVersionAdvanced(context, 1, 9, 0) == 1)
      {
        OrthancPluginStorageReadRange readRange = NULL;
        if (backend_->HasReadRange())
        {
          readRange = StorageReadRange;
        }
        
        OrthancPluginRegisterStorageArea2(context_, StorageCreate, StorageReadWhole, readRange, StorageRemove);
        hasLoadedV2 = true;
      }
#  endif
#endif

      if (!hasLoadedV2)
      {
        LOG(WARNING) << "Performance warning: Your version of the Orthanc core or SDK doesn't support reading of file ranges";
        OrthancPluginRegisterStorageArea(context_, StorageCreate, StorageRead, StorageRemove);
      }

      LOG(WARNING) << "The storage area plugin will retry up to " << backend_->GetMaxRetries()
                   << " time(s) in the case of a collision";
    }
  }


  void StorageBackend::Finalize()
  {
    backend_.reset(NULL);
    context_ = NULL;
  }


  class StorageBackend::StringVisitor : public StorageBackend::IFileContentVisitor
  {
  private:
    std::string&  target_;
    bool          success_;
      
  public:
    explicit StringVisitor(std::string& target) :
      target_(target),
      success_(false)
    {
    }

    virtual bool IsSuccess() const ORTHANC_OVERRIDE
    {
      return success_;
    }
      
    virtual void Assign(const std::string& content) ORTHANC_OVERRIDE
    {
      if (success_)
      {
        ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
      }
      else
      {
        target_.assign(content);
        success_ = true;
      }
    }
  };
    

  void StorageBackend::ReadWholeToString(std::string& target,
                                         IAccessor& accessor,
                                         const std::string& uuid,
                                         OrthancPluginContentType type)
  {
    StringVisitor visitor(target);
    accessor.ReadWhole(visitor, uuid, type);

    if (!visitor.IsSuccess())
    {
      ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
    }
  }
    

  void StorageBackend::ReadRangeToString(std::string& target,
                                         IAccessor& accessor,
                                         const std::string& uuid,
                                         OrthancPluginContentType type,
                                         uint64_t start,
                                         size_t length)
  {
    StringVisitor visitor(target);
    accessor.ReadRange(visitor, uuid, type, start, length);

    if (!visitor.IsSuccess())
    {
      ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
    }
  }


  void StorageBackend::Execute(IDatabaseOperation& operation)
  {
    std::unique_ptr<IAccessor> accessor(CreateAccessor());
    if (accessor.get() == NULL)
    {
      ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
    }
    
#if ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 9, 2)
    unsigned int attempt = 0;
#endif
    
    for (;;)
    {
      try
      {
        operation.Execute(*accessor);
        return;  // Success
      }
      catch (const Orthanc::OrthancException& e)
      {
#if ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 9, 2)
        if (e.GetErrorCode() == Orthanc::ErrorCode_DatabaseCannotSerialize)
        {
          if (attempt >= maxRetries_)
          {
            throw;
          }
          else
          {
            attempt++;
            
            // The "rand()" adds some jitter to de-synchronize writers
            boost::this_thread::sleep(boost::posix_time::milliseconds(100 * attempt + 5 * (rand() % 10)));
          }
        }
        else
        {
          throw;
        }
#else
        throw;
#endif
      }
    }
  }
}
