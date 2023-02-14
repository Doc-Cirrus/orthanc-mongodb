// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "RetryDatabaseFactory.h"

#include <Logging.h>
#include <OrthancException.h>

#include <boost/thread.hpp>


namespace OrthancDatabases
{
  IDatabase* RetryDatabaseFactory::Open()
  {
    unsigned int count = 0;

    for (;;)
    {
      try
      {
        return TryOpen();
      }
      catch (Orthanc::OrthancException& e)
      {
        if (e.GetErrorCode() == Orthanc::ErrorCode_DatabaseUnavailable)
        {
          count ++;

          if (count <= maxConnectionRetries_)
          {
            LOG(WARNING) << "Database is currently unavailable, retrying...";
            boost::this_thread::sleep(boost::posix_time::seconds(connectionRetryInterval_));
            continue;
          }
          else
          {
            LOG(ERROR) << "Timeout when connecting to the database, giving up";
          }
        }

        throw;
      }
    }
  }
}
