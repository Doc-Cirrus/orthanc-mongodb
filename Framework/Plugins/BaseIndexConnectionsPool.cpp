/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
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


#include "BaseIndexConnectionsPool.h"

#include <Logging.h>


namespace OrthancDatabases
{
  void BaseIndexConnectionsPool::HousekeepingThread(BaseIndexConnectionsPool* that)
  {
#if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 13, 0)
    Orthanc::Logging::ScopedCurrentThreadNameSetter setter("DB HOUSEKEEPING");
#elif ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 2)
    OrthancPluginSetCurrentThreadName(OrthancPlugins::GetGlobalContext(), "DB HOUSEKEEPING");    
#endif

    boost::posix_time::ptime lastInvocation = boost::posix_time::second_clock::local_time();

    while (that->housekeepingContinue_)
    {
      if (boost::posix_time::second_clock::local_time() - lastInvocation >= that->housekeepingDelay_)
      {
        try
        {
          {
            Accessor accessor(*that);
            accessor.GetBackend().PerformDbHousekeeping(accessor.GetManager());  //
          }

          that->PerformPoolHousekeeping();
        }
        catch (Orthanc::OrthancException& e)
        {
          LOG(ERROR) << "Exception during the database housekeeping: " << e.What();
        }
        catch (...)
        {
          LOG(ERROR) << "Native exception during the database houskeeping";
        }

        lastInvocation = boost::posix_time::second_clock::local_time();
      }

      boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
    }

    LOG(INFO) << "database housekeeping has stopped";
  }


  BaseIndexConnectionsPool::BaseIndexConnectionsPool(IndexBackend* backend,
                                                     unsigned int houseKeepingDelaySeconds) :
    backend_(backend),
    housekeepingContinue_(true),
    housekeepingDelay_(boost::posix_time::seconds(houseKeepingDelaySeconds))
  {
    if (backend == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }
    else if (backend->HasPerformDbHousekeeping() &&
             houseKeepingDelaySeconds == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "The delay between two executions of housekeeping cannot be zero second");
    }
    else
    {
      context_ = backend_->GetContext();
    }
  }

  
  BaseIndexConnectionsPool::~BaseIndexConnectionsPool()
  {
  }


  void BaseIndexConnectionsPool::StartHousekeepingThread()
  {
    housekeepingContinue_ = true;

    if (backend_->HasPerformDbHousekeeping())
    {
      housekeepingThread_ = boost::thread(HousekeepingThread, this);
    }
  }

  void BaseIndexConnectionsPool::StopHousekeepingThread()
  {
    housekeepingContinue_ = false;
    if (housekeepingThread_.joinable())
    {
      housekeepingThread_.join();
    }
  }


  BaseIndexConnectionsPool::Accessor::Accessor(BaseIndexConnectionsPool& pool) :
    pool_(pool),
    manager_(NULL)
  {
    // No sleep between the attempts: "GetConnection()" itself waits
    // for a connection to be released, and returns it at once
    for (;;)
    {
      manager_ = pool.GetConnection(100);
      if (manager_ != NULL)
      {
        return;
      }
    }
  }

  
  BaseIndexConnectionsPool::Accessor::~Accessor()
  {
    assert(manager_ != NULL);
    pool_.ReleaseConnection(manager_);
  }

  
  IndexBackend& BaseIndexConnectionsPool::Accessor::GetBackend() const
  {
    return *pool_.backend_;
  }

  
  DatabaseManager& BaseIndexConnectionsPool::Accessor::GetManager() const
  {
    assert(manager_ != NULL);
    return *manager_;
  }
}
