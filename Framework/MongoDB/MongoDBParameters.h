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

#if ORTHANC_ENABLE_MONGODB != 1
#  error MongoDB support must be enabled to use this file
#endif

#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <string>


namespace OrthancDatabases
{
  enum MongoDBTransactionsMode
  {
    MongoDBTransactionsMode_Auto,      // Use transactions iff the server supports them (replica set, sharded cluster)
    MongoDBTransactionsMode_Enabled,   // Require transactions, fail at startup if unavailable
    MongoDBTransactionsMode_Disabled   // Never use multi-document transactions
  };


  /**
   * Counterpart of "PostgreSQLParameters": the content of the
   * "MongoDB" section of the Orthanc configuration file.
   **/
  class MongoDBParameters
  {
  private:
    std::string              connectionUri_;
    unsigned int             chunkSize_;
    unsigned int             maxConnectionRetries_;
    unsigned int             connectionRetryInterval_;
    MongoDBTransactionsMode  transactionsMode_;
    bool                     createIndexesAtStartup_;
    bool                     enableExtendedFind_;
    unsigned int             auditLogsRetentionDays_;

    void Reset();

  public:
    MongoDBParameters();

    explicit MongoDBParameters(const OrthancPlugins::OrthancConfiguration& configuration);

    void SetConnectionUri(const std::string& uri);

    const std::string& GetConnectionUri() const
    {
      return connectionUri_;
    }

    // Name of the database, as provided in the path of the connection URI
    std::string GetDatabaseName() const;

    void SetChunkSize(unsigned int size);

    unsigned int GetChunkSize() const
    {
      return chunkSize_;
    }

    void SetMaxConnectionRetries(unsigned int retries)
    {
      maxConnectionRetries_ = retries;
    }

    unsigned int GetMaxConnectionRetries() const
    {
      return maxConnectionRetries_;
    }

    void SetConnectionRetryInterval(unsigned int seconds);

    unsigned int GetConnectionRetryInterval() const
    {
      return connectionRetryInterval_;
    }

    void SetTransactionsMode(MongoDBTransactionsMode mode)
    {
      transactionsMode_ = mode;
    }

    MongoDBTransactionsMode GetTransactionsMode() const
    {
      return transactionsMode_;
    }

    void SetCreateIndexesAtStartup(bool create)
    {
      createIndexesAtStartup_ = create;
    }

    bool IsCreateIndexesAtStartup() const
    {
      return createIndexesAtStartup_;
    }

    void SetEnableExtendedFind(bool enable)
    {
      enableExtendedFind_ = enable;
    }

    bool IsExtendedFindEnabled() const
    {
      return enableExtendedFind_;
    }

    void SetAuditLogsRetentionDays(unsigned int days)
    {
      auditLogsRetentionDays_ = days;
    }

    // 0 means that the audit logs are kept forever
    unsigned int GetAuditLogsRetentionDays() const
    {
      return auditLogsRetentionDays_;
    }

    static MongoDBTransactionsMode ParseTransactionsMode(const Json::Value& value);
  };
}
