// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IPrecompiledStatement.h"
#include "ITransaction.h"
#include "Query.h"

namespace OrthancDatabases
{
  class IDatabase : public boost::noncopyable
  {
  public:
    virtual ~IDatabase()
    {
    }

    virtual Dialect GetDialect() const = 0;

    virtual IPrecompiledStatement* Compile(const Query& query) = 0;

    virtual ITransaction* CreateTransaction(TransactionType type) = 0;
  };
}
