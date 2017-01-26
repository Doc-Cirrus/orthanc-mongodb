/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017 Osimis, Belgium
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

#include "MongoDBConnection.h"

#include <json/value.h>
#include <orthanc/OrthancCPlugin.h>


// This comes from the Orthanc source code
static const int32_t GlobalProperty_DatabaseSchemaVersion = 1;

static const int32_t GlobalProperty_IndexLock = 1024;
static const int32_t GlobalProperty_StorageLock = 1025;

static const std::string FLAG_UNLOCK = "--unlock";

namespace OrthancPlugins
{

  bool ReadConfiguration(Json::Value& configuration, OrthancPluginContext* context);
  
  std::string GetStringValue(const Json::Value& configuration, const std::string& key, const std::string& defaultValue);
  
  int GetIntegerValue(const Json::Value& configuration, const std::string& key, int defaultValue);
  
  bool GetBooleanValue(const Json::Value& configuration, const std::string& key, bool defaultValue);
  
  MongoDBConnection* CreateConnection(bool& useLock, OrthancPluginContext* context, const Json::Value& configuration);
  
  std::string GenerateUuid();

  bool IsFlagInCommandLineArguments(OrthancPluginContext* context, const std::string& flag);

} //namespace OrthancPlugins
