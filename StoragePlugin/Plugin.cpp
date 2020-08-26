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


/* Contains WIN32 define which is required for correct work of ORTHANC_PLUGINS_API with MSVC */
#include <mongoc.h>
#include <orthanc/OrthancCPlugin.h>

#include <cassert>

#include "MongoDBStorageArea.h"
#include "../Core/MongoDBException.h"
#include "../Core/Configuration.h"


static OrthancPluginContext* context_ = NULL;
static OrthancPlugins::MongoDBStorageArea* storage_ = NULL;


#if (ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER <= 0 && ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER <= 9 && ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER <= 4)
#  define RETURN_TYPE     int32_t
#  define RETURN_SUCCESS  0
#  define RETURN_FAILURE  -1
#else
#  define RETURN_TYPE     OrthancPluginErrorCode
#  define RETURN_SUCCESS  OrthancPluginErrorCode_Success
#  define RETURN_FAILURE  OrthancPluginErrorCode_Plugin
#endif


static RETURN_TYPE StorageCreate(const char* uuid,
                                            const void* content,
                                            int64_t size,
                                            OrthancPluginContentType type)
{
  try
  {
    storage_->Create(uuid, content, static_cast<size_t>(size), type);
    return RETURN_SUCCESS;
  }
  catch (std::runtime_error& e)
  {
    OrthancPluginLogError(context_, e.what());
    return RETURN_FAILURE;
  }
}


static RETURN_TYPE StorageRead(void** content,
                                          int64_t* size,
                                          const char* uuid,
                                          OrthancPluginContentType type)
{
  try
  {
    size_t tmp;
    storage_->Read(*content, tmp, uuid, type);
    *size = static_cast<int64_t>(tmp);
    return RETURN_SUCCESS;
  }
  catch (std::runtime_error& e)
  {
    OrthancPluginLogError(context_, e.what());
    return RETURN_FAILURE;
  }
}


static RETURN_TYPE StorageRemove(const char* uuid,
                                            OrthancPluginContentType type)
{
  try
  {
    storage_->Remove(uuid, type);
    return RETURN_SUCCESS;
  }
  catch (std::runtime_error& e)
  {
    OrthancPluginLogError(context_, e.what());
    return RETURN_FAILURE;
  }
}


static bool DisplayPerformanceWarning()
{
  (void) DisplayPerformanceWarning;   // Disable warning about unused function
  OrthancPluginLogWarning(context_, "Performance warning in MongoDB storage: "
                          "Non-release build, runtime debug assertions are turned on");
  return true;
}


extern "C"
{
  ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext* context)
  {
    context_ = context;
    assert(DisplayPerformanceWarning());

    mongoc_init();

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

    OrthancPluginSetDescription(context_, "Stores the files received by Orthanc into a MongoDB database.");


    Json::Value configuration;
    if (!OrthancPlugins::ReadConfiguration(configuration, context))
    {
      OrthancPluginLogError(context_, "Unable to read the configuration file");
      return -1;
    }


    if (!configuration.isMember("MongoDB") ||
        configuration["MongoDB"].type() != Json::objectValue ||
        !OrthancPlugins::GetBooleanValue(configuration["MongoDB"], "EnableStorage", false))
    {
      OrthancPluginLogWarning(context_, "The MongoDB storage area is currently disabled, set \"EnableStorage\" to \"true\" in the \"MongoDB\" section of the configuration file of Orthanc");
      return 0;
    }
    else
    {
      OrthancPluginLogWarning(context_, "Using MongoDB storage area");
    }

    try
    {
      std::unique_ptr<OrthancPlugins::MongoDBConnection>
        pg(OrthancPlugins::CreateConnection(context_, configuration));

      /* Create the storage area back-end */
      storage_ = new OrthancPlugins::MongoDBStorageArea(pg.release());

      /* Register the storage area into Orthanc */
      OrthancPluginRegisterStorageArea(context_, StorageCreate, StorageRead, StorageRemove);
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
    OrthancPluginLogWarning(context_, "Storage plugin is finalizing");

    if (storage_ != NULL)
    {
      delete storage_;
      storage_ = NULL;
    }

    mongoc_cleanup();
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetName()
  {
    return "MongoDBStorage";
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetVersion()
  {
    return "1.7.3";
  }
}
