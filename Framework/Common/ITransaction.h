// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "Dictionary.h"
#include "IPrecompiledStatement.h"
#include "IResult.h"

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

    virtual IResult* Execute(IPrecompiledStatement& statement,
                             const Dictionary& parameters) = 0;

    virtual void ExecuteWithoutResult(IPrecompiledStatement& statement,
                                      const Dictionary& parameters) = 0;

    virtual bool DoesTableExist(const std::string& name) = 0;

    virtual bool DoesTriggerExist(const std::string& name) = 0;  // Only for MySQL

    virtual void ExecuteMultiLines(const std::string& query) = 0;
  };
}
