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

#include <boost/lexical_cast.hpp>
#include <algorithm>


namespace OrthancDatabases
{
  static const unsigned int DEFAULT_CHUNK_SIZE = 261120;  // 255 KiB, the default chunk size of GridFS

  // The separate connection options of the releases <= 1.9.1, which "ConnectionUri" overrides
  static const char* const CONNECTION_OPTIONS[] = {
    "host", "port", "database", "user", "password", "authenticationDatabase"
  };


  static std::string EncodeUriComponent(const std::string& s)
  {
    // Percent-encoding of everything but the unreserved characters of RFC 3986
    static const char* const HEX = "0123456789ABCDEF";

    std::string result;
    result.reserve(s.size());

    for (size_t i = 0; i < s.size(); i++)
    {
      const unsigned char c = static_cast<unsigned char>(s[i]);

      if ((c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') ||
          c == '-' || c == '.' || c == '_' || c == '~')
      {
        result.push_back(static_cast<char>(c));
      }
      else
      {
        result.push_back('%');
        result.push_back(HEX[c >> 4]);
        result.push_back(HEX[c & 0x0f]);
      }
    }

    return result;
  }


  static std::string LookupNonEmptyString(const OrthancPlugins::OrthancConfiguration& configuration,
                                          const std::string& key)
  {
    // An empty value counts as absent, e.g. "user" : "${MONGODB_USER}" with no such variable
    std::string value;
    if (configuration.LookupStringValue(value, key))
    {
      return value;
    }
    else
    {
      return "";
    }
  }


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
    if (configuration.LookupStringValue(uri, "ConnectionUri") &&
        !uri.empty())
    {
      std::string ignored;
      for (size_t i = 0; i < sizeof(CONNECTION_OPTIONS) / sizeof(CONNECTION_OPTIONS[0]); i++)
      {
        if (configuration.GetJson().isMember(CONNECTION_OPTIONS[i]))
        {
          ignored += std::string(ignored.empty() ? "" : ", ") + "\"" + CONNECTION_OPTIONS[i] + "\"";
        }
      }

      if (!ignored.empty())
      {
        LOG(WARNING) << "MongoDB: \"ConnectionUri\" is set, so these options are ignored: " << ignored;
      }

      SetConnectionUri(uri);
    }
    else if (HasConnectionOptions(configuration))
    {
      const std::string built = BuildConnectionUri(configuration);

      // Checked here rather than by "SetConnectionUri()", whose exception would log the
      // message of the driver, which can contain the URI, hence the password
      bool valid;
      try
      {
        mongocxx::uri parsed(built);
        valid = true;
      }
      catch (mongocxx::exception&)
      {
        valid = false;
      }

      if (!valid)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                        "Invalid MongoDB connection options: check \"host\", \"port\", "
                                        "\"database\", \"user\", \"password\" and \"authenticationDatabase\"");
      }

      SetConnectionUri(built);
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "No MongoDB connection provided: set \"ConnectionUri\", or \"database\" "
                                      "(with \"host\", \"port\"... if needed)");
    }
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


  bool MongoDBParameters::HasConnectionOptions(const OrthancPlugins::OrthancConfiguration& configuration)
  {
    for (size_t i = 0; i < sizeof(CONNECTION_OPTIONS) / sizeof(CONNECTION_OPTIONS[0]); i++)
    {
      if (configuration.GetJson().isMember(CONNECTION_OPTIONS[i]))
      {
        return true;
      }
    }

    return false;
  }


  std::string MongoDBParameters::BuildConnectionUri(const OrthancPlugins::OrthancConfiguration& configuration)
  {
    std::string host = "localhost";
    if (configuration.GetJson().isMember("host"))
    {
      host = configuration.GetStringValue("host", "");
    }

    if (host.empty() ||
        host.find_first_of("/?#@,[] \t") != std::string::npos)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "The MongoDB option \"host\" must be one host name or address; "
                                      "use \"ConnectionUri\" for several hosts");
    }

    const size_t colons = std::count(host.begin(), host.end(), ':');
    if (colons == 1)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "The MongoDB option \"host\" cannot contain the port, "
                                      "use the option \"port\"");
    }
    else if (colons > 1)
    {
      host = "[" + host + "]";  // IPv6 address
    }

    unsigned int port = 27017;
    if (configuration.LookupUnsignedIntegerValue(port, "port") &&
        (port == 0 || port > 65535))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "The MongoDB option \"port\" must be between 1 and 65535");
    }

    const std::string database = LookupNonEmptyString(configuration, "database");
    if (database.empty())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "The MongoDB option \"database\" is required, unless \"ConnectionUri\" is set");
    }

    // Characters that MongoDB forbids in the name of a database (on Linux), and its maximum length
    if (database.find_first_of("/\\. \"$") != std::string::npos ||
        database.find('\0') != std::string::npos ||
        database.size() >= 64)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "Invalid name of MongoDB database in the option \"database\": " + database);
    }

    const std::string user = LookupNonEmptyString(configuration, "user");
    const std::string password = LookupNonEmptyString(configuration, "password");
    const std::string authenticationDatabase = LookupNonEmptyString(configuration, "authenticationDatabase");

    if (user.empty() &&
        !password.empty())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "The MongoDB option \"password\" is set without the option \"user\"");
    }

    std::string uri = "mongodb://";

    if (!user.empty())
    {
      uri += EncodeUriComponent(user);

      if (!password.empty())
      {
        uri += ":" + EncodeUriComponent(password);
      }

      uri += "@";
    }

    uri += host + ":" + boost::lexical_cast<std::string>(port) + "/" + database;

    if (!authenticationDatabase.empty())
    {
      uri += "?authSource=" + EncodeUriComponent(authenticationDatabase);
    }

    return uri;
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
