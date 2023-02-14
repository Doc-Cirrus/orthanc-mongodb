// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "Utf8StringValue.h"

#include "BinaryStringValue.h"
#include "NullValue.h"
#include "Integer64Value.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>

namespace OrthancDatabases
{
  IValue* Utf8StringValue::Convert(ValueType target) const
  {
    switch (target)
    {
      case ValueType_Null:
        return new NullValue;

      case ValueType_BinaryString:
        return new BinaryStringValue(utf8_);

      case ValueType_Integer64:
        try
        {
          int64_t value = boost::lexical_cast<int64_t>(utf8_);
          return new Integer64Value(value);
        }
        catch (boost::bad_lexical_cast&)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat);
        }

        break;

      case ValueType_Utf8String:
        return new Utf8StringValue(utf8_);

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }
}
