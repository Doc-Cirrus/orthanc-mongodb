// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IValue.h"

#include <Compatibility.h>

namespace OrthancDatabases
{
  class NullValue : public IValue
  {
  public:
    virtual ValueType GetType() const ORTHANC_OVERRIDE
    {
      return ValueType_Null;
    }

    virtual IValue* Convert(ValueType target) const ORTHANC_OVERRIDE;
  };
}
