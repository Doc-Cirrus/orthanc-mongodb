// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include <boost/noncopyable.hpp>
#include <string>

namespace OrthancDatabases
{
  class ITransaction : public boost::noncopyable
  {
  public:
    virtual ~ITransaction()
    {
    }

    virtual bool IsImplicit() const = 0;

    virtual void Rollback() = 0;

    virtual void Commit() = 0;

    virtual bool DoesTableExist(const std::string& name) = 0;

    virtual bool DoesTriggerExist(const std::string& name) = 0;  // Only for MySQL
  };
}
