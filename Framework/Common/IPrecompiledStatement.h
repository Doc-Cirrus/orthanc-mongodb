// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include <boost/noncopyable.hpp>

namespace OrthancDatabases
{
  class IPrecompiledStatement : public boost::noncopyable
  {
  public:
    virtual ~IPrecompiledStatement()
    {
    }
  };
}
