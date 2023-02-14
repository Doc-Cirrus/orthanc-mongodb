// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IValue.h"

#include <Compatibility.h>

#include <stdint.h>

namespace OrthancDatabases
{
  class Integer64Value : public IValue
  {
  private:
    int64_t  value_;

  public:
    explicit Integer64Value(int64_t value) :
    value_(value)
    {
    }

    int64_t GetValue() const
    {
      return value_;
    }

    virtual ValueType GetType() const ORTHANC_OVERRIDE
    {
      return ValueType_Integer64;
    }

    virtual IValue* Convert(ValueType target) const ORTHANC_OVERRIDE;
  };
}
