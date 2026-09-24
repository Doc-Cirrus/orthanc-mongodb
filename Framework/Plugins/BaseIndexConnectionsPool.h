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


#pragma once

#include "IdentifierTag.h"
#include "IndexBackend.h"

#include <MultiThreading/SharedMessageQueue.h>
#include <MultiThreading/Semaphore.h>

#include <list>
#include <boost/thread.hpp>

namespace OrthancDatabases
{
  class BaseIndexConnectionsPool : public boost::noncopyable
  {
  protected:
    std::unique_ptr<IndexBackend>  backend_;
    OrthancPluginContext*          context_;

    bool                           housekeepingContinue_;
    boost::thread                  housekeepingThread_;
    boost::posix_time::time_duration  housekeepingDelay_;

    static void HousekeepingThread(BaseIndexConnectionsPool* that);

    virtual void PerformPoolHousekeeping() = 0;

    void StartHousekeepingThread();

    void StopHousekeepingThread();

    /**
     * Returns an available connection, waiting up to the timeout for
     * one to be released, or NULL if none became available. The wait
     * must end as soon as "ReleaseConnection()" is called, otherwise
     * a busy pool delays the requests by the polling period.
     **/
    virtual DatabaseManager* GetConnection(unsigned int millisecondsTimeout) = 0;
    
    virtual void ReleaseConnection(DatabaseManager* manager) = 0;

  public:
    BaseIndexConnectionsPool(IndexBackend* backend /* takes ownership */,
                             unsigned int houseKeepingDelaySeconds);

    virtual ~BaseIndexConnectionsPool();

    OrthancPluginContext* GetContext() const
    {
      return context_;
    }

    virtual void OpenConnections(bool hasIdentifierTags,
                                 const std::list<IdentifierTag>& identifierTags) = 0;

    virtual void CloseConnections() = 0;

    class Accessor : public boost::noncopyable
    {
    private:
      BaseIndexConnectionsPool&                pool_;
      DatabaseManager*                         manager_;
      
    public:
      explicit Accessor(BaseIndexConnectionsPool& pool);

      ~Accessor();

      IndexBackend& GetBackend() const;

      DatabaseManager& GetManager() const;
    };
  };
}
