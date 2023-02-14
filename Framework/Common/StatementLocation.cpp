// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "StatementLocation.h"

#include <string.h>

namespace OrthancDatabases
{
  bool StatementLocation::operator< (const StatementLocation& other) const
  {
    if (line_ != other.line_)
    {
      return line_ < other.line_;
    }
    else
    {
      return strcmp(file_, other.file_) < 0;
    }
  }
}
