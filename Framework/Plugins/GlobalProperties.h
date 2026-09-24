/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#pragma once

#define MISSING_SERVER_IDENTIFIER ""


namespace Orthanc
{
  /**
   * The enum "GlobalProperty" is a subset of the "GlobalProperty_XXX"
   * values from the Orthanc server that have a special meaning to the
   * database plugins:
   * https://orthanc.uclouvain.be/hg/orthanc/file/default/OrthancServer/Sources/ServerEnumerations.h
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
