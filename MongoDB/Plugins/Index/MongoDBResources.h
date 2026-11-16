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
#include <string>
#include <vector>


namespace OrthancDatabases
{
  /**
   * The "Resources" collection. Each document is:
   *
   *   { internalId, resourceType, publicId,
   *     parentId  (absent for patients),
   *     "0": [...], "1": [...], "2": [...], "3": [...],
   *     sorts: [...]  (studies and series only),
   *     instancePublicId }
   *
   * Documents written by older versions of the plugin may have a
   * null "parentId".
   *
   * where the arrays "0" to "3" list, for each level, the internal
   * IDs of the ancestors of the resource, of the resource itself, and
   * of its descendants. "internalId" comes from the "Resources"
   * sequence.
   **/
  class MongoDBResources : public boost::noncopyable
  {
  private:
    DatabaseManager&  manager_;
    MongoDBDatabase&  database_;

    std::optional<bsoncxx::document::value> LookupByInternalId(int64_t internalId);

    bsoncxx::document::value GetByInternalId(int64_t internalId);

    std::optional<bsoncxx::document::value> LookupByPublicId(const char* publicId,
                                                              OrthancPluginResourceType type);

    /**
     * Finds the resource "publicId" at the level "levels.size()", or
     * creates it under the ancestors "levels". Appends its internal ID
     * to "levels", and returns whether this call created it.
     *
     * The creation is a single upsert on "publicId", so that concurrent
     * calls agree on one creator: with the unique index on "publicId",
     * the server retries the upsert that loses the race, which then
     * finds the resource of the winner.
     **/
    bool FindOrCreate(std::vector<int64_t>& levels,
                      const char* publicId,
                      const char* instancePublicId);

    /**
     * Without transactions, the second pass of "DeleteResource()", once
     * the resources "ids" are gone. A store that ran meanwhile may have
     * created resources under them, or written to them. The new
     * resources and their descendants are deleted and appended to
     * "ids", then what refers to "ids" is deleted again.
     **/
    void SweepAfterDelete(IDatabaseBackendOutput& output,
                          std::list<int64_t>& ids);

    // Undoes "CreateInstance()" when an ancestor was deleted meanwhile
    void RemoveCreatedInstance(const OrthancPluginCreateInstanceResult& created);

  public:
    static const char* const COLLECTION;

    explicit MongoDBResources(DatabaseManager& manager);

    int64_t CreateResource(const char* publicId,
                           OrthancPluginResourceType type);

    void CreateInstance(OrthancPluginCreateInstanceResult& result /*out*/,
                        const char* hashPatient,
                        const char* hashStudy,
                        const char* hashSeries,
                        const char* hashInstance);

    void AttachChild(int64_t parent,
                     int64_t child);

    void DeleteResource(IDatabaseBackendOutput& output,
                        int64_t id);

    void GetAllInternalIds(std::list<int64_t>& target /*out*/,
                           OrthancPluginResourceType resourceType);

    void GetAllPublicIds(std::list<std::string>& target /*out*/,
                         OrthancPluginResourceType resourceType);

    void GetAllPublicIds(std::list<std::string>& target /*out*/,
                         OrthancPluginResourceType resourceType,
                         int64_t since,
                         uint32_t limit);

    void GetChildrenInternalId(std::list<int64_t>& target /*out*/,
                               int64_t id);

    void GetChildrenPublicId(std::list<std::string>& target /*out*/,
                             int64_t id);

    void GetChildrenMetadata(std::list<std::string>& target /*out*/,
                             int64_t resourceId,
                             int32_t metadata);

    std::string GetPublicId(int64_t resourceId);

    uint64_t GetResourcesCount(OrthancPluginResourceType resourceType);

    uint64_t GetAllResourcesCount();

    OrthancPluginResourceType GetResourceType(int64_t resourceId);

    bool IsExistingResource(int64_t internalId);

    bool LookupParent(int64_t& parentId /*out*/,
                      int64_t resourceId);

    bool LookupResource(int64_t& id /*out*/,
                        OrthancPluginResourceType& type /*out*/,
                        const char* publicId);

    bool LookupResourceAndParent(int64_t& id /*out*/,
                                 OrthancPluginResourceType& type /*out*/,
                                 std::string& parentPublicId /*out*/,
                                 const char* publicId);
  };
}
