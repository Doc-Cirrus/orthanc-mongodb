// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "DatabasesEnumerations.h"

#include <OrthancException.h>

namespace OrthancDatabases
{
  const char* EnumerationToString(ValueType type)
  {
    switch (type)
    {
      case ValueType_BinaryString:
        return "BinaryString";

      case ValueType_InputFile:
        return "InputFile";

      case ValueType_Integer64:
        return "Integer64";

      case ValueType_Null:
        return "Null";

      case ValueType_ResultFile:
        return "ResultFile";

      case ValueType_Utf8String:
        return "Utf8String";

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }
}
