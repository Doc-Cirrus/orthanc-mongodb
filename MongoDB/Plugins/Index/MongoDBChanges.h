/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/


#pragma once

#include <set>

#include "../../../Framework/Common/DatabaseManager.h"
#include "../../../Framework/MongoDB/MongoDBDatabase.h"
#include "../../../Framework/Plugins/IDatabaseBackendOutput.h"

#include <list>


namespace OrthancDatabases
{
  /**
   * The "Changes" collection: { id, changeType, internalId,
   * resourceType, date }, where "id" comes from the "Changes" sequence.
   **/
  class MongoDBChanges : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

  public:
    explicit MongoDBChanges(DatabaseManager& manager);

    void LogChange(int32_t changeType,
                   int64_t resourceId,
                   OrthancPluginResourceType resourceType,
                   const char* date);

    void GetChanges(IDatabaseBackendOutput& output,
                    bool& done /*out*/,
                    int64_t since,
                    uint32_t maxResults);

    void GetChangesExtended(IDatabaseBackendOutput& output,
                            bool& done /*out*/,
                            int64_t since,
                            int64_t to,
                            const std::set<uint32_t>& changeTypes,
                            uint32_t maxResults);

    void GetLastChange(IDatabaseBackendOutput& output);

    void ClearChanges();

    int64_t GetLastChangeIndex();

    // The changes of deleted resources are deleted as well
    void DeleteForResources(const std::list<int64_t>& resources);
  };
}
