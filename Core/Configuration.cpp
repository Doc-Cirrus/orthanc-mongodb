/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017  (Doc Cirrus GmbH)   Ronald Wertlen, Ihor Mozil
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

#include "Configuration.h"
#include "MongoDBException.h"

#include <fstream>
#include <json/reader.h>
#include <memory>

// For UUID generation
extern "C"
{
#ifdef WIN32
#include <rpc.h>
#else
#include <uuid/uuid.h>
#endif
}

namespace OrthancPlugins
{

bool ReadConfiguration(Json::Value &configuration, OrthancPluginContext *context)
{
  std::string s;

  char *tmp = OrthancPluginGetConfiguration(context);
  if (tmp == NULL)
  {
    OrthancPluginLogError(context, "Error while retrieving the configuration from Orthanc");
    return false;
  }

  s.assign(tmp);
  OrthancPluginFreeString(context, tmp);

  Json::Reader reader;
  if (reader.parse(s, configuration))
  {
    return true;
  }
  else
  {
    OrthancPluginLogError(context, "Unable to parse the configuration");
    return false;
  }
}

std::string GetStringValue(const Json::Value &configuration, const std::string &key, const std::string &defaultValue)
{
  if (configuration.type() != Json::objectValue || !configuration.isMember(key) ||
      configuration[key].type() != Json::stringValue)
  {
    return defaultValue;
  }
  else
  {
    return configuration[key].asString();
  }
}

int GetIntegerValue(const Json::Value &configuration, const std::string &key, int defaultValue)
{
  if (configuration.type() != Json::objectValue || !configuration.isMember(key) ||
      configuration[key].type() != Json::intValue)
  {
    return defaultValue;
  }
  else
  {
    return configuration[key].asInt();
  }
}

bool GetBooleanValue(const Json::Value &configuration, const std::string &key, bool defaultValue)
{
  if (configuration.type() != Json::objectValue || !configuration.isMember(key) ||
      configuration[key].type() != Json::booleanValue)
  {
    return defaultValue;
  }
  else
  {
    return configuration[key].asBool();
  }
}

MongoDBConnection *CreateConnection(OrthancPluginContext *context, const Json::Value &configuration)
{
  std::unique_ptr<MongoDBConnection> connection = std::make_unique<MongoDBConnection>();

  if (configuration.isMember("MongoDB"))
  {
    Json::Value c = configuration["MongoDB"];
    if (c.isMember("ConnectionUri"))
    {
      connection->SetConnectionUri(c["ConnectionUri"].asString());
    }
    if (c.isMember("ChunkSize"))
    {
      connection->SetChunkSize(c["ChunkSize"].asInt());
    }
    if (c.isMember("host"))
    {
      connection->SetHost(c["host"].asString());
    }
    if (c.isMember("port"))
    {
      connection->SetTcpPort(c["port"].asInt());
    }
    if (c.isMember("database"))
    {
      connection->SetDatabase(c["database"].asString());
    }
    if (c.isMember("user"))
    {
      connection->SetUser(c["user"].asString());
    }
    if (c.isMember("password"))
    {
      connection->SetPassword(c["password"].asString());
    }
    if (c.isMember("authenticationDatabase"))
    {
      connection->SetAuthenticationDatabase(c["authenticationDatabase"].asString());
    }
  }
  return connection.release();
}

std::string GenerateUuid()
{
#ifdef WIN32
  UUID uuid;
  UuidCreate(&uuid);

  unsigned char *str;
  UuidToStringA(&uuid, &str);

  std::string s((char *)str);

  RpcStringFreeA(&str);
#else
  uuid_t uuid;
  uuid_generate_random(uuid);
  char s[37];
  uuid_unparse(uuid, s);
#endif
  return s;
}

bool IsFlagInCommandLineArguments(OrthancPluginContext *context, const std::string &flag)
{
  uint32_t count = OrthancPluginGetCommandLineArgumentsCount(context);

  for (uint32_t i = 0; i < count; i++)
  {
    char *tmp = OrthancPluginGetCommandLineArgument(context, i);
    std::string arg(tmp);
    OrthancPluginFreeString(context, tmp);

    if (arg == flag)
    {
      return true;
    }
  }

  return false;
}

} // namespace OrthancPlugins
