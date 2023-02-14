#include <mongoc.h>
#include "MongoDBIndex.h"

#include "../../Framework/Plugins/PluginInitialization.h"
#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Logging.h>


extern "C"
{
ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext *context) {
    if (!OrthancDatabases::InitializePlugin(context, "MongoDB", true)) {
        return -1;
    }

    OrthancPlugins::OrthancConfiguration configuration;

    if (!configuration.IsSection("MongoDB")) {
        LOG(WARNING) << "No available configuration for the MongoDB index plugin";
        return 0;
    }

    OrthancPlugins::OrthancConfiguration mongodb;
    configuration.GetSection(mongodb, "MongoDB");

    bool enable;
    if (!mongodb.LookupBooleanValue(enable, "EnableIndex") || !enable) {
        LOG(WARNING) << "The MongoDB index is currently disabled, set \"EnableIndex\" "
                     << "to \"true\" in the \"MongoDB\" section of the configuration file of Orthanc";
        return 0;
    }

    try {
        /* Register the MongoDB index into Orthanc */
        mongoc_init();

        const std::string connectionUri = mongodb.GetStringValue("ConnectionUri", "");
        const unsigned int chunkSize = mongodb.GetUnsignedIntegerValue("ChunkSize", 261120);

        const unsigned int countConnections = mongodb.GetUnsignedIntegerValue("IndexConnectionsCount", 5);
        const unsigned int maxConnectionRetries = mongodb.GetUnsignedIntegerValue("MaxConnectionRetries", 10);

        if (connectionUri.empty()) {
            throw Orthanc::OrthancException(
                    Orthanc::ErrorCode_ParameterOutOfRange,
                    "No connection string provided for the MongoDB index"
            );
        }

        OrthancDatabases::IndexBackend::Register(
                new OrthancDatabases::MongoDBIndex(context, connectionUri, chunkSize),
                countConnections, maxConnectionRetries
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
    LOG(WARNING) << "MongoDB index is finalizing";
    OrthancDatabases::IndexBackend::Finalize();

    mongoc_cleanup();
}


ORTHANC_PLUGINS_API const char *OrthancPluginGetName() {
    return "mongodb-index";
}


ORTHANC_PLUGINS_API const char *OrthancPluginGetVersion() {
    return ORTHANC_PLUGIN_VERSION;
}
}
