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
#include "BaseIndexConnectionsPool.h"

#include <MultiThreading/SharedMessageQueue.h>
#include <MultiThreading/Semaphore.h>

#include <list>
#include <boost/thread.hpp>

namespace OrthancDatabases
{
  class DynamicIndexConnectionsPool : public BaseIndexConnectionsPool
  {
  private:
    boost::mutex                   connectionsMutex_;
    size_t                         maxConnectionsCount_;
    Orthanc::Semaphore             connectionsSemaphore_;
    std::list<DatabaseManager*>    connections_;
    Orthanc::Semaphore             availableConnectionsSemaphore_;
    std::list<DatabaseManager*>    availableConnections_;
    boost::condition_variable      connectionReleased_;   // Signaled under "connectionsMutex_"

    void CleanupOldConnections();

  protected:
    virtual DatabaseManager* GetConnection(unsigned int millisecondsTimeout) ORTHANC_OVERRIDE;
    
    virtual void ReleaseConnection(DatabaseManager* manager) ORTHANC_OVERRIDE;

    virtual void PerformPoolHousekeeping() ORTHANC_OVERRIDE;

  public:
    DynamicIndexConnectionsPool(IndexBackend* backend /* takes ownership */,
                                size_t maxConnectionsCount,
                                unsigned int houseKeepingDelaySeconds);

    virtual ~DynamicIndexConnectionsPool() ORTHANC_OVERRIDE;

    virtual void OpenConnections(bool hasIdentifierTags,
                                 const std::list<IdentifierTag>& identifierTags) ORTHANC_OVERRIDE;

    virtual void CloseConnections() ORTHANC_OVERRIDE;
  };
}
