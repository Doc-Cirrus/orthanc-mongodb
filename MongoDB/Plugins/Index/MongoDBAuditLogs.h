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

#include "../../../Framework/Common/DatabaseManager.h"
#include "../../../Framework/MongoDB/MongoDBDatabase.h"
#include "../../../Framework/Plugins/IDatabaseBackend.h"

#include <list>
#include <string>


namespace OrthancDatabases
{
  /**
   * The "AuditLogs" collection { ts, sourcePlugin, userId, resourceType,
   * resourceId, action, logData }, where "ts" is a date and the
   * optional "logData" is binary. The option "AuditLogsRetentionDays"
   * turns the index on "ts" into a TTL index (cf. "MongoDBSchema").
   **/
  class MongoDBAuditLogs : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

  public:
    static const char* const COLLECTION;

    explicit MongoDBAuditLogs(DatabaseManager& manager);

    void RecordAuditLog(const std::string& sourcePlugin,
                        const std::string& userId,
                        OrthancPluginResourceType resourceType,
                        const std::string& resourceId,
                        const std::string& action,
                        const void* logData,
                        uint32_t logDataSize);

    /**
     * Same filters as the SQL plugins: empty strings are no filter,
     * "since" is the number of logs to skip, "limit" 0 means no limit,
     * and the logs come in chronological order.
     **/
    void GetAuditLogs(std::list<IDatabaseBackend::AuditLog>& logs,
                      const std::string& userIdFilter,
                      const std::string& resourceIdFilter,
                      const std::string& actionFilter,
                      const std::string& fromTsIsoFormat,
                      const std::string& toTsIsoFormat,
                      uint64_t since,
                      uint64_t limit);
  };
}
