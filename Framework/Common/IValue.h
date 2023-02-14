// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "DatabasesEnumerations.h"

#include <boost/noncopyable.hpp>
#include <string>

namespace OrthancDatabases
{
  class IValue : public boost::noncopyable
  {
  public:
    virtual ~IValue()
    {
    }

    virtual ValueType GetType() const = 0;

    virtual IValue* Convert(ValueType target) const = 0;
  };
}
