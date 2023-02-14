// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "ResultBase.h"

#include "../Common/BinaryStringValue.h"
#include "../Common/Integer64Value.h"
#include "../Common/NullValue.h"
#include "../Common/Utf8StringValue.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>
#include <OrthancException.h>

#include <cassert>
#include <memory>

namespace OrthancDatabases
{
  void ResultBase::ClearFields()
  {
    for (size_t i = 0; i < fields_.size(); i++)
    {
      if (fields_[i] != NULL)
      {
        delete fields_[i];
        fields_[i] = NULL;
      }
    }
  }


  void ResultBase::ConvertFields()
  {
    assert(expectedType_.size() == fields_.size() &&
           hasExpectedType_.size() == fields_.size());

    for (size_t i = 0; i < fields_.size(); i++)
    {
      if (fields_[i] == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
      }

      ValueType sourceType = fields_[i]->GetType();
      ValueType targetType = expectedType_[i];

      if (hasExpectedType_[i] &&
          sourceType != ValueType_Null &&
          sourceType != targetType)
      {
        std::unique_ptr<IValue> converted(fields_[i]->Convert(targetType));

        if (converted.get() == NULL)
        {
          LOG(ERROR) << "Cannot convert between data types from a database";
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadParameterType);
        }
        else
        {
          assert(fields_[i] != NULL);
          delete fields_[i];
          fields_[i] = converted.release();
        }
      }
    }
  }


  void ResultBase::FetchFields()
  {
    ClearFields();

    if (!IsDone())
    {
      for (size_t i = 0; i < fields_.size(); i++)
      {
        fields_[i] = FetchField(i);

        if (fields_[i] == NULL)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
        }
      }

      ConvertFields();
    }
  }


  void ResultBase::SetFieldsCount(size_t count)
  {
    if (!fields_.empty())
    {
      // This method can only be invoked once
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }

    fields_.resize(count);
    expectedType_.resize(count, ValueType_Null);
    hasExpectedType_.resize(count, false);
  }


  void ResultBase::SetExpectedType(size_t field,
                                   ValueType type)
  {
    assert(expectedType_.size() == fields_.size() &&
           hasExpectedType_.size() == fields_.size());

    if (field < fields_.size())
    {
      expectedType_[field] = type;
      hasExpectedType_[field] = true;

      if (!IsDone())
      {
        ConvertFields();
      }
    }
  }


  const IValue& ResultBase::GetField(size_t index) const
  {
    if (IsDone())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else if (index >= fields_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else if (fields_[index] == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
    }
    else
    {
      return *fields_[index];
    }
  }
}
