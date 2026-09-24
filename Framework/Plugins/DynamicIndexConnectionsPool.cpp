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


#include "DynamicIndexConnectionsPool.h"

#include <Logging.h>


namespace OrthancDatabases
{
  void DynamicIndexConnectionsPool::PerformPoolHousekeeping()
  {
    CleanupOldConnections();

    OrthancPluginSetMetricsValue(OrthancPlugins::GetGlobalContext(), "orthanc_index_active_connections", maxConnectionsCount_ - connectionsSemaphore_.GetAvailableResourcesCount(), OrthancPluginMetricsType_Default);
  }


  DynamicIndexConnectionsPool::DynamicIndexConnectionsPool(IndexBackend* backend,
                                                           size_t maxConnectionsCount,
                                                           unsigned int houseKeepingDelaySeconds) :
    BaseIndexConnectionsPool(backend, houseKeepingDelaySeconds),
    maxConnectionsCount_(maxConnectionsCount),
    connectionsSemaphore_(maxConnectionsCount),
    availableConnectionsSemaphore_(0)
  {
    if (maxConnectionsCount == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "There must be a non-zero number of connections to the database");
    }
  }

  
  DynamicIndexConnectionsPool::~DynamicIndexConnectionsPool()
  {
    boost::mutex::scoped_lock  lock(connectionsMutex_);

    for (std::list<DatabaseManager*>::iterator
           it = connections_.begin(); it != connections_.end(); ++it)
    {
      assert(*it != NULL);
      delete *it;
    }
  }


  void DynamicIndexConnectionsPool::OpenConnections(bool hasIdentifierTags,
                                             const std::list<IdentifierTag>& identifierTags)
  {
    assert(backend_.get() != NULL);

    DynamicIndexConnectionsPool::Accessor accessor(*this);
    backend_->ConfigureDatabase(accessor.GetManager(), hasIdentifierTags, identifierTags);

    StartHousekeepingThread();
  }


  void DynamicIndexConnectionsPool::CloseConnections()
  {
    StopHousekeepingThread();

    boost::mutex::scoped_lock  lock(connectionsMutex_);

    for (std::list<DatabaseManager*>::iterator
            it = connections_.begin(); it != connections_.end(); ++it)
    {
      assert(*it != NULL);
      (*it)->Close();
    }
  }

  DatabaseManager* DynamicIndexConnectionsPool::GetConnection(unsigned int millisecondsTimeout)
  {
    const boost::system_time deadline = (boost::get_system_time() +
                                         boost::posix_time::milliseconds(millisecondsTimeout));

    boost::mutex::scoped_lock  lock(connectionsMutex_);

    for (;;)
    {
      if (availableConnectionsSemaphore_.TryAcquire(1)) // there is a connection directly available, take it
      {
        // LOG(INFO) << "--- Reusing an available connection";

        DatabaseManager* manager = availableConnections_.front();
        availableConnections_.pop_front();
        return manager;
      }
      else if (connectionsSemaphore_.TryAcquire(1))  // no connection directly available, check if we can create a new one
      {
        // LOG(INFO) << "--- Creating a new connection";

        connections_.push_back(new DatabaseManager(backend_->CreateDatabaseFactory()));
        connections_.back()->GetDatabase();  // Make sure to open the database connection

        // no need to push it in the availableConnections since it is being used immediately
        return connections_.back();
      }
      else if (!connectionReleased_.timed_wait(lock, deadline))  // unable to get a connection in time
      {
        return NULL;
      }
    }
  }

  void DynamicIndexConnectionsPool::CleanupOldConnections()
  {
    boost::mutex::scoped_lock  lock(connectionsMutex_);

    while (availableConnectionsSemaphore_.TryAcquire(1))
    {
      DatabaseManager* manager = availableConnections_.front();
      if (manager->GetElapsedSecondsSinceLastUse() > 60 || manager->GetElapsedSecondsSinceCreation() > 3600)
      {
        // LOG(INFO) << "--- Deleting an old connection";

        availableConnections_.pop_front();
        connections_.remove(manager);      
        
        delete manager;
        connectionsSemaphore_.Release(1);
        connectionReleased_.notify_one();  // A new connection may be created
      }
      else
      {
        availableConnectionsSemaphore_.Release(1);  // we have not consumed it
        break;
      }
    }

  }

  void DynamicIndexConnectionsPool::ReleaseConnection(DatabaseManager* manager)
  {
    boost::mutex::scoped_lock  lock(connectionsMutex_);
    availableConnections_.push_front(manager);
    availableConnectionsSemaphore_.Release(1);
    connectionReleased_.notify_one();
  }

}
