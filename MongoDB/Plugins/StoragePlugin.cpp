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


#include "MongoDBStorageArea.h"
#include "../../Framework/MongoDB/MongoDBDatabase.h"
#include "../../Framework/MongoDB/MongoDBParameters.h"
#include "../../Framework/Plugins/PluginInitialization.h"
#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Logging.h>

#include <memory>


#define ORTHANC_PLUGIN_NAME "mongodb-storage"


// The MongoDB C++ driver must be initialized exactly once per shared library
static std::unique_ptr<mongocxx::instance> mongoInstance_;


extern "C"
{
  ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext* context)
  {
    if (!OrthancDatabases::InitializePlugin(context, ORTHANC_PLUGIN_NAME, "MongoDB", false))
    {
      return -1;
    }

    OrthancPlugins::OrthancConfiguration configuration;

    if (!configuration.IsSection("MongoDB"))
    {
      LOG(WARNING) << "No available configuration for the MongoDB storage area plugin";
      return 0;
    }

    OrthancPlugins::OrthancConfiguration mongodb;
    configuration.GetSection(mongodb, "MongoDB");

    bool enable;
    if (!mongodb.LookupBooleanValue(enable, "EnableStorage") ||
        !enable)
    {
      LOG(WARNING) << "The MongoDB storage area is currently disabled, set \"EnableStorage\" "
                   << "to \"true\" in the \"MongoDB\" section of the configuration file of Orthanc";
      return 0;
    }

    try
    {
      if (mongoInstance_.get() == NULL)
      {
        OrthancDatabases::MongoDBDatabase::KeepPluginLoaded();
        mongoInstance_.reset(new mongocxx::instance);
      }

      OrthancDatabases::MongoDBParameters parameters(mongodb);
      OrthancDatabases::StorageBackend::Register(context, new OrthancDatabases::MongoDBStorageArea(parameters));
    }
    catch (Orthanc::OrthancException& e)
    {
      LOG(ERROR) << e.What();
      return -1;
    }
    catch (std::exception& e)
    {
      LOG(ERROR) << "Exception while initializing the MongoDB storage area plugin: " << e.what();
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
    LOG(WARNING) << "MongoDB storage area is finalizing";
    OrthancDatabases::StorageBackend::Finalize();

    mongoInstance_.reset();
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
