// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "Dictionary.h"

#include "BinaryStringValue.h"
#include "InputFileValue.h"
#include "Integer64Value.h"
#include "NullValue.h"
#include "Utf8StringValue.h"

#include <Logging.h>
#include <OrthancException.h>

#include <cassert>

namespace OrthancDatabases
{
  void Dictionary::Clear()
  {
    for (Values::iterator it = values_.begin();
         it != values_.end(); ++it)
    {
      assert(it->second != NULL);
      delete it->second;
    }

    values_.clear();
  }


  bool Dictionary::HasKey(const std::string& key) const
  {
    return values_.find(key) != values_.end();
  }


  void Dictionary::Remove(const std::string& key)
  {
    Values::iterator found = values_.find(key);

    if (found != values_.end())
    {
      assert(found->second != NULL);
      delete found->second;
      values_.erase(found);
    }
  }


  void Dictionary::SetValue(const std::string& key,
                            IValue* value)   // Takes ownership
  {
    if (value == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    Values::iterator found = values_.find(key);

    if (found == values_.end())
    {
      values_[key] = value;
    }
    else
    {
      assert(found->second != NULL);
      delete found->second;
      found->second = value;
    }
  }


  void Dictionary::SetUtf8Value(const std::string& key,
                                const std::string& utf8)
  {
    SetValue(key, new Utf8StringValue(utf8));
  }


  void Dictionary::SetBinaryValue(const std::string& key,
                                  const std::string& binary)
  {
    SetValue(key, new BinaryStringValue(binary));
  }


  void Dictionary::SetFileValue(const std::string& key,
                                const std::string& file)
  {
    SetValue(key, new InputFileValue(file));
  }


  void Dictionary::SetFileValue(const std::string& key,
                                const void* content,
                                size_t size)
  {
    SetValue(key, new InputFileValue(content, size));
  }


  void Dictionary::SetIntegerValue(const std::string& key,
                                   int64_t value)
  {
    SetValue(key, new Integer64Value(value));
  }


  void Dictionary::SetNullValue(const std::string& key)
  {
    SetValue(key, new NullValue);
  }


  const IValue& Dictionary::GetValue(const std::string& key) const
  {
    Values::const_iterator found = values_.find(key);

    if (found == values_.end())
    {
      LOG(ERROR) << "Inexistent value in a dictionary: " << key;
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentItem);
    }
    else
    {
      assert(found->second != NULL);
      return *found->second;
    }
  }
}
