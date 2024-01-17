// Taken from https://hg.orthanc-server.com/orthanc-databases/

/**
 * NOTE: Until Orthanc 1.4.0, this file was part of the Orthanc source
 * distribution. This file is now part of "orthanc-databases", in
 * order to uncouple its evolution from the Orthanc core.
 **/

#pragma once

#include "IDatabaseBackend.h"


namespace OrthancDatabases
{
  /**
   * @brief Bridge between C and C++ database engines.
   *
   * Class creating the bridge between the C low-level primitives for
   * custom database engines, and the high-level IDatabaseBackend C++
   * interface, for Orthanc <= 1.9.1.
   *
   * @ingroup Callbacks
   **/
  class DatabaseBackendAdapterV2
  {
  private:
    // This class cannot be instantiated
    DatabaseBackendAdapterV2()
    {
    }

  public:
    class Adapter;
    class Output;

    class Factory : public IDatabaseBackendOutput::IFactory
    {
    private:
      OrthancPluginContext*         context_;
      OrthancPluginDatabaseContext* database_;

    public:
      Factory(OrthancPluginContext*         context,
              OrthancPluginDatabaseContext* database) :
        context_(context),
        database_(database)
      {
      }

      virtual IDatabaseBackendOutput* CreateOutput() ORTHANC_OVERRIDE;
    };

    static void Register(IDatabaseBackend* backend);

    static void Finalize();
  };
}
