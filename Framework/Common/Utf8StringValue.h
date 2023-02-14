// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IValue.h"

#include <Compatibility.h>

namespace OrthancDatabases
{
  // Represents an UTF-8 string
  class Utf8StringValue : public IValue
  {
  private:
    std::string  utf8_;

  public:
    explicit Utf8StringValue(const std::string& utf8) :
      utf8_(utf8)
    {
    }

    const std::string& GetContent() const
    {
      return utf8_;
    }

    virtual ValueType GetType() const ORTHANC_OVERRIDE
    {
      return ValueType_Utf8String;
    }

    virtual IValue* Convert(ValueType target) const ORTHANC_OVERRIDE;
  };
}
