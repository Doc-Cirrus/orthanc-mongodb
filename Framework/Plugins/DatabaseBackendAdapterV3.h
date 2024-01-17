// Taken from https://hg.orthanc-server.com/orthanc-databases/


#pragma once

#include "IndexBackend.h"


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)         // Macro introduced in Orthanc 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 9, 2)

namespace OrthancDatabases
{
  /**
   * @brief Bridge between C and C++ database engines.
   *
   * Class creating the bridge between the C low-level primitives for
   * custom database engines, and the high-level IDatabaseBackend C++
   * interface, for Orthanc >= 1.9.2.
   **/
  class DatabaseBackendAdapterV3
  {
  private:
    class Output;

    // This class cannot be instantiated
    DatabaseBackendAdapterV3()
    {
    }

  public:
    class Adapter;
    class Transaction;

    class Factory : public IDatabaseBackendOutput::IFactory
    {
    public:
      virtual IDatabaseBackendOutput* CreateOutput() ORTHANC_OVERRIDE;
    };

    static void Register(IndexBackend* backend,
                         size_t countConnections,
                         unsigned int maxDatabaseRetries);

    static void Finalize();
  };
}

#  endif
#endif
