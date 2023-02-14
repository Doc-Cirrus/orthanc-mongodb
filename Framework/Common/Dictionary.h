// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IValue.h"

#include <map>
#include <stdint.h>

namespace OrthancDatabases
{
  class Dictionary : public boost::noncopyable
  {
  private:
    typedef std::map<std::string, IValue*>   Values;

    Values  values_;

  public:
    ~Dictionary()
    {
      Clear();
    }

    void Clear();

    bool HasKey(const std::string& key) const;

    void Remove(const std::string& key);

    void SetValue(const std::string& key,
                  IValue* value);   // Takes ownership

    void SetUtf8Value(const std::string& key,
                      const std::string& utf8);

    void SetBinaryValue(const std::string& key,
                        const std::string& binary);

    void SetFileValue(const std::string& key,
                      const std::string& file);

    void SetFileValue(const std::string& key,
                      const void* content,
                      size_t size);

    void SetIntegerValue(const std::string& key,
                         int64_t value);

    void SetNullValue(const std::string& key);

    const IValue& GetValue(const std::string& key) const;
  };
}
