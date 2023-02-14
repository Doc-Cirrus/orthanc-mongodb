// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "BinaryStringValue.h"

#include "NullValue.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>

namespace OrthancDatabases
{
  IValue* BinaryStringValue::Convert(ValueType target) const
  {
    switch (target)
    {
      case ValueType_Null:
        return new NullValue;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadParameterType);
    }
  }
}
