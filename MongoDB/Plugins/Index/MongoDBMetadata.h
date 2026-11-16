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

#include <list>
#include <map>
#include <string>


namespace OrthancDatabases
{
  /**
   * The "Metadata" collection { id, type, value, revision }, with one
   * document per metadata of a resource.
   **/
  class MongoDBMetadata : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

  public:
    explicit MongoDBMetadata(DatabaseManager& manager);

    void SetMetadata(int64_t id,
                     int32_t metadataType,
                     const char* value,
                     int64_t revision);

    // Several metadata at once, cf. "SetResourcesContent()"
    void SetMetadata(uint32_t count,
                     const OrthancPluginResourcesContentMetadata* metadata);

    void DeleteMetadata(int64_t id,
                        int32_t metadataType);

    bool LookupMetadata(std::string& target /*out*/,
                        int64_t& revision /*out*/,
                        int64_t id,
                        int32_t metadataType);

    void ListAvailableMetadata(std::list<int32_t>& target /*out*/,
                               int64_t id);

    void GetAllMetadata(std::map<int32_t, std::string>& result /*out*/,
                        int64_t id);

    void GetMetadataOfResources(std::list<std::string>& target /*out*/,
                                const std::list<int64_t>& resources,
                                int32_t metadataType);

    void DeleteForResources(const std::list<int64_t>& resources);
  };
}
