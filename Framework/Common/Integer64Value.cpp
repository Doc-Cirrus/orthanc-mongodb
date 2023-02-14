// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "Integer64Value.h"

#include "BinaryStringValue.h"
#include "NullValue.h"
#include "Utf8StringValue.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>

namespace OrthancDatabases
{
  IValue* Integer64Value::Convert(ValueType target) const
  {
    std::string s = boost::lexical_cast<std::string>(value_);

    switch (target)
    {
      case ValueType_Null:
        return new NullValue;

      case ValueType_BinaryString:
        return new BinaryStringValue(s);

      case ValueType_Utf8String:
        return new Utf8StringValue(s);

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }
}
