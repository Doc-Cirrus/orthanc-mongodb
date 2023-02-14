// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "InputFileValue.h"

#include "BinaryStringValue.h"
#include "NullValue.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>

namespace OrthancDatabases
{
  IValue* InputFileValue::Convert(ValueType target) const
  {
    switch (target)
    {
      case ValueType_BinaryString:
        return new BinaryStringValue(content_);

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadParameterType);
    }
  }
}
