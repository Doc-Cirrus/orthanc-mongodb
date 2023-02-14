// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include <orthanc/OrthancCPlugin.h>

#include <string>

namespace OrthancDatabases
{
  bool InitializePlugin(OrthancPluginContext* context,
                        const std::string& dbms,
                        bool isIndex);
}
