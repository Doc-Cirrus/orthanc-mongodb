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

#include "MongoDBIndex.h"
#include "../../Framework/MongoDB/MongoDBDatabase.h"
#include "../../Framework/MongoDB/MongoDBParameters.h"
#include "../../Framework/Plugins/PluginInitialization.h"

#include <Logging.h>

#include <google/protobuf/any.h>
#include <google/protobuf/stubs/common.h>

#include <memory>

#define ORTHANC_PLUGIN_NAME "mongodb-index"


// The MongoDB C++ driver must be initialized exactly once per shared library
static std::unique_ptr<mongocxx::instance> mongoInstance_;


extern "C"
{
  ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext* context)
  {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if (!OrthancDatabases::InitializePlugin(context, ORTHANC_PLUGIN_NAME, "MongoDB", true))
    {
      return -1;
    }

    OrthancPlugins::OrthancConfiguration configuration;

    if (!configuration.IsSection("MongoDB"))
    {
      LOG(WARNING) << "No available configuration for the MongoDB index plugin";
      return 0;
    }

    OrthancPlugins::OrthancConfiguration mongodb;
    configuration.GetSection(mongodb, "MongoDB");

    bool enable;
    if (!mongodb.LookupBooleanValue(enable, "EnableIndex") ||
        !enable)
    {
      LOG(WARNING) << "The MongoDB index is currently disabled, set \"EnableIndex\" "
                   << "to \"true\" in the \"MongoDB\" section of the configuration file of Orthanc";
      return 0;
    }

    bool readOnly = configuration.GetBooleanValue("ReadOnly", false);

    if (readOnly)
    {
      LOG(WARNING) << "READ-ONLY SYSTEM: the Database plugin is working in read-only mode";
    }

    try
    {
      if (mongoInstance_.get() == NULL)
      {
        OrthancDatabases::MongoDBDatabase::KeepPluginLoaded();
        mongoInstance_.reset(new mongocxx::instance);
      }

      const size_t countConnections = mongodb.GetUnsignedIntegerValue("IndexConnectionsCount", 5);
      const bool useDynamicConnectionPool = mongodb.GetBooleanValue("UseDynamicConnectionPool", false);
      const unsigned int housekeepingDelaySeconds = mongodb.GetUnsignedIntegerValue("HousekeepingInterval", 1);

      OrthancDatabases::MongoDBParameters parameters(mongodb);
      OrthancDatabases::IndexBackend::Register(
        new OrthancDatabases::MongoDBIndex(context, parameters, readOnly),
        countConnections, useDynamicConnectionPool, parameters.GetMaxConnectionRetries(), housekeepingDelaySeconds);
    }
    catch (Orthanc::OrthancException& e)
    {
      LOG(ERROR) << e.What();
      return -1;
    }
    catch (std::exception& e)
    {
      LOG(ERROR) << "Exception while initializing the MongoDB index plugin: " << e.what();
      return -1;
    }
    catch (...)
    {
      LOG(ERROR) << "Native exception while initializing the plugin";
      return -1;
    }

    return 0;
  }


  ORTHANC_PLUGINS_API void OrthancPluginFinalize()
  {
    LOG(WARNING) << "MongoDB index is finalizing";
    OrthancDatabases::IndexBackend::Finalize();

    mongoInstance_.reset();

    google::protobuf::ShutdownProtobufLibrary();
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetName()
  {
    return ORTHANC_PLUGIN_NAME;
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetVersion()
  {
    return ORTHANC_PLUGIN_VERSION;
  }
}
