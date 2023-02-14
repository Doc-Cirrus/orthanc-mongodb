// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IValue.h"

#include <Compatibility.h>

namespace OrthancDatabases
{
  class BinaryStringValue : public IValue
  {
  private:
    std::string  content_;

  public:
    explicit BinaryStringValue(const std::string& content) :
      content_(content)
    {
    }

    const std::string& GetContent() const
    {
      return content_;
    }

    const void* GetBuffer() const
    {
      return (content_.empty() ? NULL : content_.c_str());
    }

    size_t GetSize() const
    {
      return content_.size();
    }

    virtual ValueType GetType() const ORTHANC_OVERRIDE
    {
      return ValueType_BinaryString;
    }

    virtual IValue* Convert(ValueType target) const ORTHANC_OVERRIDE;
  };
}
