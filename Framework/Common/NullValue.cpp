// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "NullValue.h"

#include "Utf8StringValue.h"

#include <OrthancException.h>

namespace OrthancDatabases
{
  IValue* NullValue::Convert(ValueType target) const
  {
    switch (target)
    {
      case ValueType_Null:
        return new NullValue;

      case ValueType_Utf8String:
        return new Utf8StringValue("(null)");

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }
}
