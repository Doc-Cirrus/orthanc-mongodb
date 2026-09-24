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


#include "MongoDBParameters.h"
#include "MongoDBIncludes.h"

#include <Logging.h>
#include <OrthancException.h>
#include <Toolbox.h>


namespace OrthancDatabases
{
  static const unsigned int DEFAULT_CHUNK_SIZE = 261120;  // 255 KiB, the default chunk size of GridFS


  void MongoDBParameters::Reset()
  {
    connectionUri_.clear();
    chunkSize_ = DEFAULT_CHUNK_SIZE;
    maxConnectionRetries_ = 10;
    connectionRetryInterval_ = 5;
    transactionsMode_ = MongoDBTransactionsMode_Auto;
    createIndexesAtStartup_ = true;
    enableExtendedFind_ = true;
    auditLogsRetentionDays_ = 0;
  }


  MongoDBParameters::MongoDBParameters()
  {
    Reset();
  }


  MongoDBParameters::MongoDBParameters(const OrthancPlugins::OrthancConfiguration& configuration)
  {
    Reset();

    std::string uri;
    if (!configuration.LookupStringValue(uri, "ConnectionUri") ||
        uri.empty())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "No connection string (\"ConnectionUri\") provided for MongoDB");
    }

    SetConnectionUri(uri);
    SetChunkSize(configuration.GetUnsignedIntegerValue("ChunkSize", DEFAULT_CHUNK_SIZE));

    // "MaxConnectionRetries" is the name used by releases <= 1.11 of this plugin
    unsigned int retries;
    if (configuration.LookupUnsignedIntegerValue(retries, "MaximumConnectionRetries") ||
        configuration.LookupUnsignedIntegerValue(retries, "MaxConnectionRetries"))
    {
      maxConnectionRetries_ = retries;
    }

    SetConnectionRetryInterval(configuration.GetUnsignedIntegerValue("ConnectionRetryInterval", 5));

    if (configuration.GetJson().isMember("EnableTransactions"))
    {
      transactionsMode_ = ParseTransactionsMode(configuration.GetJson()["EnableTransactions"]);
    }

    createIndexesAtStartup_ = configuration.GetBooleanValue("CreateIndexesAtStartup", true);
    enableExtendedFind_ = configuration.GetBooleanValue("EnableExtendedFind", true);
    auditLogsRetentionDays_ = configuration.GetUnsignedIntegerValue("AuditLogsRetentionDays", 0);
  }


  void MongoDBParameters::SetConnectionUri(const std::string& uri)
  {
    try
    {
      mongocxx::uri parsed(uri);  // Validate the syntax of the URI

      if (parsed.database().empty())
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                        "The MongoDB connection URI must contain the name of the database, "
                                        "e.g. \"mongodb://localhost:27017/orthanc\"");
      }
    }
    catch (mongocxx::exception& e)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "Invalid MongoDB connection URI: " + std::string(e.what()));
    }

    connectionUri_ = uri;
  }


  std::string MongoDBParameters::GetDatabaseName() const
  {
    return std::string(mongocxx::uri(connectionUri_).database());
  }


  void MongoDBParameters::SetChunkSize(unsigned int size)
  {
    if (size == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "The GridFS chunk size must be strictly positive");
    }

    chunkSize_ = size;
  }


  void MongoDBParameters::SetConnectionRetryInterval(unsigned int seconds)
  {
    if (seconds == 0)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    connectionRetryInterval_ = seconds;
  }


  MongoDBTransactionsMode MongoDBParameters::ParseTransactionsMode(const Json::Value& value)
  {
    if (value.type() == Json::booleanValue)
    {
      return value.asBool() ? MongoDBTransactionsMode_Enabled : MongoDBTransactionsMode_Disabled;
    }
    else if (value.type() == Json::stringValue)
    {
      std::string s = value.asString();
      Orthanc::Toolbox::ToLowerCase(s);

      if (s == "auto")
      {
        return MongoDBTransactionsMode_Auto;
      }
      else if (s == "true")
      {
        return MongoDBTransactionsMode_Enabled;
      }
      else if (s == "false")
      {
        return MongoDBTransactionsMode_Disabled;
      }
    }

    throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat,
                                    "The option \"MongoDB.EnableTransactions\" must be \"Auto\", true or false");
  }
}
