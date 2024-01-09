// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include <boost/noncopyable.hpp>

#include "DatabasesEnumerations.h"
#include "ITransaction.h"

namespace OrthancDatabases
{
  class IDatabase : public boost::noncopyable
  {
  public:
    virtual ~IDatabase()
    {
    }

    virtual ITransaction* CreateTransaction(TransactionType type) = 0;
  };
}
