/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 DocCirrus, Germany
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General 
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#include <assert.h>
#include <memory>

#include <orthanc/OrthancCPlugin.h>
#include <mongoc.h>

#include "MongoDBBackend.h"
#include "../Core/MongoDBException.h"
#include "../Core/Configuration.h"

static OrthancPluginContext* context_ = NULL;
static OrthancPlugins::MongoDBBackend* backend_ = NULL;

static bool DisplayPerformanceWarning()
{
  (void)DisplayPerformanceWarning;   // Disable warning about unused function
  OrthancPluginLogWarning(context_, "Performance warning in MongoDB index: "
    "Non-release build, runtime debug assertions are turned on");
  return true;
}


extern "C"
{
  ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext* context)
  {

    mongoc_init();

    context_ = context;
    assert(DisplayPerformanceWarning());

    /* Check the version of the Orthanc core */
    if (OrthancPluginCheckVersion(context_) == 0)
    {
      char info[1024];
      sprintf(info, "Your version of Orthanc (%s) must be above %d.%d.%d to run this plugin",
      context_->orthancVersion,
      ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER,
      ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER,
      ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER);
      OrthancPluginLogError(context_, info);
      return -1;
    }

    OrthancPluginSetDescription(context_, "Stores the Orthanc index into a MongoDB database.");

    Json::Value configuration;
    
    if (!OrthancPlugins::ReadConfiguration(configuration, context))
    {
      OrthancPluginLogError(context_, "Unable to read the configuration file");
      return -1;
    }
    
    if (!configuration.isMember("MongoDB") || configuration["MongoDB"].type() != Json::objectValue ||
      !OrthancPlugins::GetBooleanValue(configuration["MongoDB"], "EnableIndex", false))
    {
      OrthancPluginLogWarning(context_, "The MongoDB index is currently disabled, set \"EnableIndex\" to \"true\" in the \"MongoDB\" section of the configuration file of Orthanc");
      return 0;
    }
    else
    {
      OrthancPluginLogWarning(context_, "Using MongoDB index");
    }
    
    try
    {
      /* Create the connection to MongoDB */
      std::unique_ptr<OrthancPlugins::MongoDBConnection> mongoconnection(OrthancPlugins::CreateConnection(context_, configuration));

      /* Create the database back-end */
      backend_ = new OrthancPlugins::MongoDBBackend(context_, mongoconnection.release());

      /* Register the MongoDB index into Orthanc */
      OrthancPlugins::DatabaseBackendAdapter::Register(context_, *backend_);
    }
    catch (std::runtime_error& e)
    {
      OrthancPluginLogError(context_, e.what());
      return -1;
    }
    
    return 0;
  }

  
  ORTHANC_PLUGINS_API void OrthancPluginFinalize()
  {
    OrthancPluginLogWarning(context_, "MongoDB index is finalizing");

    if (backend_ != NULL)
    {
      delete backend_;
      backend_ = NULL;
    }

    mongoc_cleanup();
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetName()
  {
    return "MongoDBIndex";
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetVersion()
  {
    return "1.0.0";
  }

} //extern "C"

