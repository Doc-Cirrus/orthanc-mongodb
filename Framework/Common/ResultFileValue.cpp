// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "ResultFileValue.h"

#include "BinaryStringValue.h"
#include "NullValue.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>

namespace OrthancDatabases
{
  IValue* ResultFileValue::Convert(ValueType target) const
  {
    switch (target)
    {
      case ValueType_BinaryString:
      {
        std::string content;
        ReadWhole(content);
        return new BinaryStringValue(content);
      }

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadParameterType);
    }
  }
}
