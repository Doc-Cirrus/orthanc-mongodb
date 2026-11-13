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


#pragma once

#include "../../../Framework/Common/DatabaseManager.h"
#include "../../../Framework/MongoDB/MongoDBDatabase.h"
#include "../../../Framework/Plugins/IDatabaseBackendOutput.h"


namespace OrthancDatabases
{
  /**
   * The "ExportedResources" collection, whose "id" comes from the
   * "ExportedResources" sequence.
   **/
  class MongoDBExportedResources : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

    void Answer(IDatabaseBackendOutput& output,
                bool& done,
                const bsoncxx::document::view_or_value& filter,
                int sort,
                uint32_t maxResults);

  public:
    explicit MongoDBExportedResources(DatabaseManager& manager);

    void LogExportedResource(OrthancPluginResourceType resourceType,
                             const char* publicId,
                             const char* modality,
                             const char* date,
                             const char* patientId,
                             const char* studyInstanceUid,
                             const char* seriesInstanceUid,
                             const char* sopInstanceUid);

    void GetExportedResources(IDatabaseBackendOutput& output,
                              bool& done /*out*/,
                              int64_t since,
                              uint32_t maxResults);

    void GetLastExportedResource(IDatabaseBackendOutput& output);

    void ClearExportedResources();
  };
}
