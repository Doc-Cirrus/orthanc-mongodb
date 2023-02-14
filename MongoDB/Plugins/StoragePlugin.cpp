

#include <mongoc.h>
#include "MongoDBStorageArea.h"

#include "../../Framework/Plugins/PluginInitialization.h"
#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Logging.h>


extern "C"
{
ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext *context) {
    if (!OrthancDatabases::InitializePlugin(context, "MongoDB", false)) {
        return -1;
    }

    OrthancPlugins::OrthancConfiguration configuration;

    if (!configuration.IsSection("MongoDB")) {
        LOG(WARNING) << "No available configuration for the MongoDB storage area plugin";
        return 0;
    }

    OrthancPlugins::OrthancConfiguration mongodb;
    configuration.GetSection(mongodb, "MongoDB");

    bool enable;
    if (!mongodb.LookupBooleanValue(enable, "EnableStorage") ||
        !enable) {
        LOG(WARNING) << "The MongoDB storage area is currently disabled, set \"EnableStorage\" "
                     << "to \"true\" in the \"MongoDB\" section of the configuration file of Orthanc";
        return 0;
    }

    try {
        /* Register the MongoDB storage into Orthanc */
        mongoc_init();

        const std::string connectionUri = mongodb.GetStringValue("ConnectionUri", "");
        const unsigned int chunkSize = mongodb.GetUnsignedIntegerValue("ChunkSize", 261120);

        // const unsigned int countConnections = mongodb.GetUnsignedIntegerValue("IndexConnectionsCount", 5);
        const unsigned int maxConnectionRetries = mongodb.GetUnsignedIntegerValue("MaxConnectionRetries", 10);

        if (connectionUri.empty()) {
            throw Orthanc::OrthancException(
                    Orthanc::ErrorCode_ParameterOutOfRange,
                    "No connection string provided for the MongoDB index"
            );
        }

        OrthancDatabases::MongoDBStorageArea::Register(
                context, new OrthancDatabases::MongoDBStorageArea(
                        connectionUri, static_cast<int>(chunkSize), static_cast<int>(maxConnectionRetries)
                )
        );
    }
    catch (Orthanc::OrthancException &e) {
        LOG(ERROR) << e.What();
        return -1;
    }
    catch (...) {
        LOG(ERROR) << "Native exception while initializing the plugin";
        return -1;
    }

    return 0;
}


ORTHANC_PLUGINS_API void OrthancPluginFinalize() {
    LOG(WARNING) << "MongoDB storage area is finalizing";
    OrthancDatabases::MongoDBStorageArea::Finalize();

    mongoc_cleanup();
}


ORTHANC_PLUGINS_API const char *OrthancPluginGetName() {
    return "mongodb-storage";
}


ORTHANC_PLUGINS_API const char *OrthancPluginGetVersion() {
    return ORTHANC_PLUGIN_VERSION;
}
}
