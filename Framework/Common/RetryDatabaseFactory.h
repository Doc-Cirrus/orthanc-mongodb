// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IDatabaseFactory.h"

#include <Compatibility.h>

namespace OrthancDatabases
{
  class RetryDatabaseFactory : public IDatabaseFactory
  {
  private:
    unsigned int  maxConnectionRetries_;
    unsigned int  connectionRetryInterval_;

  protected:
    virtual IDatabase* TryOpen() = 0;

  public:
    RetryDatabaseFactory(unsigned int maxConnectionRetries,
                         unsigned int connectionRetryInterval) :
      maxConnectionRetries_(maxConnectionRetries),
      connectionRetryInterval_(connectionRetryInterval)
    {
    }

    virtual IDatabase* Open() ORTHANC_OVERRIDE;
  };
}
