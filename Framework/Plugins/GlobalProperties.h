// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#define MISSING_SERVER_IDENTIFIER ""


namespace Orthanc
{
  /**
   * The enum "GlobalProperty" is a subset of the "GlobalProperty_XXX"
   * values from the Orthanc server that have a special meaning to the
   * database plugins:
   * https://hg.orthanc-server.com/orthanc/file/default/OrthancServer/Sources/ServerEnumerations.h
   *
   * WARNING: The values must be the same between the Orthanc core and
   * this enum!
   **/

  enum GlobalProperty
  {
    GlobalProperty_DatabaseSchemaVersion = 1,   // Unused in the Orthanc core as of Orthanc 0.9.5
    GlobalProperty_GetTotalSizeIsFast = 6,      // New in Orthanc 1.5.2

    // Reserved values for internal use by the database plugins
    GlobalProperty_DatabasePatchLevel = 4,
    GlobalProperty_DatabaseInternal0 = 10,
    GlobalProperty_DatabaseInternal1 = 11,
    GlobalProperty_DatabaseInternal2 = 12,
    GlobalProperty_DatabaseInternal3 = 13,
    GlobalProperty_DatabaseInternal4 = 14,
    GlobalProperty_DatabaseInternal5 = 15,
    GlobalProperty_DatabaseInternal6 = 16,
    GlobalProperty_DatabaseInternal7 = 17,
    GlobalProperty_DatabaseInternal8 = 18,
    GlobalProperty_DatabaseInternal9 = 19   // Only used in unit tests
  };
}
