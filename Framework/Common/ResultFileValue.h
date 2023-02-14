// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IValue.h"

#include <Compatibility.h>

#include <stdint.h>

namespace OrthancDatabases
{
  /**
   * This class is not used for MySQL, as MySQL uses BLOB columns to
   * store files.
   **/
  class ResultFileValue : public IValue
  {
  public:
    virtual void ReadWhole(std::string& target) const = 0;

    virtual void ReadRange(std::string& target,
                           uint64_t start,
                           size_t length) const = 0;

    virtual ValueType GetType() const ORTHANC_OVERRIDE
    {
      return ValueType_ResultFile;
    }

    virtual IValue* Convert(ValueType target) const ORTHANC_OVERRIDE;
  };
}
