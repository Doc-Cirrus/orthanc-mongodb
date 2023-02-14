// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IDatabase.h"

namespace OrthancDatabases
{
  class IDatabaseFactory : public boost::noncopyable
  {
  public:
    virtual ~IDatabaseFactory()
    {
    }

    virtual IDatabase* Open() = 0;
  };
}
