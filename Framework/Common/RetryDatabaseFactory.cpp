/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#include "RetryDatabaseFactory.h"

#include <Logging.h>
#include "../../Resources/Orthanc/Plugins/OrthancPluginException.h"

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
