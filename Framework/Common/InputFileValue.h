// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IValue.h"

#include <Compatibility.h>

namespace OrthancDatabases
{
  class InputFileValue : public IValue
  {
  private:
    std::string  content_;

  public:
    explicit InputFileValue(const std::string& content) :
      content_(content)
    {
    }

    InputFileValue(const void* buffer,
                   size_t size)
    {
      content_.assign(reinterpret_cast<const char*>(buffer), size);
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
      return ValueType_InputFile;
    }

    virtual IValue* Convert(ValueType target) const ORTHANC_OVERRIDE;
  };
}
