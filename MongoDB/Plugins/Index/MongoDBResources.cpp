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


#include "MongoDBResources.h"

#include "MongoDBAttachments.h"
#include "MongoDBChanges.h"
#include "MongoDBMainDicomTags.h"
#include "MongoDBLabels.h"
#include "MongoDBMetadata.h"
#include "MongoDBPatientRecycling.h"
#include "MongoDBStatistics.h"
#include "../../../Framework/MongoDB/MongoDBToolbox.h"

#include <OrthancException.h>

#include <set>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_array;
  using bsoncxx::builder::basic::make_document;


  const char* const MongoDBResources::COLLECTION = "Resources";


  static std::string GetLevelKey(int32_t level)
  {
    return std::to_string(level);
  }


  static OrthancPluginResourceType GetType(const bsoncxx::document::view& resource)
  {
    return static_cast<OrthancPluginResourceType>(MongoDBToolbox::GetInt32(resource, "resourceType"));
  }


  /**
   * The document of a new resource, except its "publicId" (that the
   * filter of the upsert provides). "levels" gives the internal IDs of
   * its ancestors, followed by its own internal ID.
   **/
  static bsoncxx::document::value CreateResourceDocument(const std::vector<int64_t>& levels,
                                                         const char* instancePublicId)
  {
    if (levels.empty() || levels.size() > 4)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    const int32_t level = static_cast<int32_t>(levels.size()) - 1;

    bsoncxx::builder::basic::document resource;
    resource.append(kvp("internalId", levels.back()),
                    kvp("resourceType", level));

    // Patients have no "parentId" (older versions wrote null)
    if (level > 0)
    {
      resource.append(kvp("parentId", levels[level - 1]));
    }

    for (int32_t i = 0; i <= 3; i++)
    {
      if (i <= level)
      {
        resource.append(kvp(GetLevelKey(i), make_array(levels[i])));
      }
      else
      {
        resource.append(kvp(GetLevelKey(i), make_array()));
      }
    }

    if (level == OrthancPluginResourceType_Study ||
        level == OrthancPluginResourceType_Series)
    {
      resource.append(kvp("sorts", make_array()));
    }

    if (instancePublicId != NULL)
    {
      resource.append(kvp("instancePublicId", instancePublicId));
    }

    return resource.extract();
  }


  MongoDBResources::MongoDBResources(DatabaseManager& manager) :
    manager_(manager),
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  std::optional<bsoncxx::document::value> MongoDBResources::LookupByInternalId(int64_t internalId)
  {
    return database_.GetCollection(COLLECTION).FindOne(make_document(kvp("internalId", internalId)));
  }


  bsoncxx::document::value MongoDBResources::GetByInternalId(int64_t internalId)
  {
    std::optional<bsoncxx::document::value> resource = LookupByInternalId(internalId);

    if (resource)
    {
      return *resource;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
    }
  }


  std::optional<bsoncxx::document::value> MongoDBResources::LookupByPublicId(const char* publicId,
                                                                             OrthancPluginResourceType type)
  {
    return database_.GetCollection(COLLECTION).FindOne(
      make_document(kvp("publicId", publicId), kvp("resourceType", static_cast<int32_t>(type))));
  }


  int64_t MongoDBResources::CreateResource(const char* publicId,
                                           OrthancPluginResourceType type)
  {
    if (static_cast<int32_t>(type) < 0 ||
        static_cast<int32_t>(type) > 3)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    // Same document shape as in "CreateInstance()", except that the
    // parent is only known once "AttachChild()" is called: the level
    // arrays of the ancestors are temporarily left empty, and
    // "parentId" is only set by "AttachChild()"
    const int64_t id = database_.GetNextSequence(COLLECTION);

    bsoncxx::builder::basic::document resource;
    resource.append(kvp("internalId", id),
                    kvp("resourceType", static_cast<int32_t>(type)),
                    kvp("publicId", publicId));

    for (int32_t i = 0; i <= 3; i++)
    {
      if (i == static_cast<int32_t>(type))
      {
        resource.append(kvp(GetLevelKey(i), make_array(id)));
      }
      else
      {
        resource.append(kvp(GetLevelKey(i), make_array()));
      }
    }

    if (type == OrthancPluginResourceType_Study ||
        type == OrthancPluginResourceType_Series)
    {
      resource.append(kvp("sorts", make_array()));
    }

    if (type == OrthancPluginResourceType_Instance)
    {
      resource.append(kvp("instancePublicId", publicId));
    }

    database_.GetCollection(COLLECTION).InsertOne(resource.view());

    MongoDBStatisticsValues change;
    change.counts_[type] = 1;
    MongoDBStatistics(manager_).RecordChange(change);

    if (type == OrthancPluginResourceType_Patient)
    {
      MongoDBPatientRecycling(manager_).AddPatient(id);
    }

    return id;
  }


  /**
   * The public ID of a series or an instance is derived from those of
   * its ancestors, so a resource found under another parent than the
   * expected one was left by a store that raced a deletion without
   * transactions, and whose ancestors were re-created since (cf.
   * "RemoveCreatedInstance()"). That store removes it, so the caller
   * retries.
   **/
  static void CheckParent(const bsoncxx::document::view& resource,
                          const std::vector<int64_t>& ancestors)
  {
    int64_t parent;
    if (!ancestors.empty() &&
        (!MongoDBToolbox::LookupInteger(parent, resource, "parentId") ||
         parent != ancestors.back()))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_DatabaseCannotSerialize,
                                      "The resource " + MongoDBToolbox::GetString(resource, "publicId") +
                                      " is left from a deleted parent");
    }
  }


  bool MongoDBResources::FindOrCreate(std::vector<int64_t>& levels,
                                      const char* publicId,
                                      const char* instancePublicId)
  {
    const int32_t level = static_cast<int32_t>(levels.size());

    std::optional<bsoncxx::document::value> existing =
      LookupByPublicId(publicId, static_cast<OrthancPluginResourceType>(level));

    if (existing)
    {
      CheckParent(existing->view(), levels);
      levels.push_back(MongoDBToolbox::GetInteger(existing->view(), "internalId"));
      return false;
    }

    // If another call creates the resource first, this ID is never used
    std::vector<int64_t> created = levels;
    created.push_back(database_.GetNextSequence(COLLECTION));

    mongocxx::options::find_one_and_update options;
    options.upsert(true);
    options.return_document(mongocxx::options::return_document::k_after);

    std::optional<bsoncxx::document::value> resource = database_.GetCollection(COLLECTION).FindOneAndUpdate(
      make_document(kvp("publicId", publicId)),
      make_document(kvp("$setOnInsert", CreateResourceDocument(created, instancePublicId))),
      options);

    if (!resource ||
        MongoDBToolbox::GetInt32(resource->view(), "resourceType") != level)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_Database,
                                      "Two resources of different levels have the public ID " + std::string(publicId));
    }

    const int64_t id = MongoDBToolbox::GetInteger(resource->view(), "internalId");
    if (id != created.back())
    {
      CheckParent(resource->view(), levels);
    }

    levels.push_back(id);
    return (id == created.back());
  }


  void MongoDBResources::CreateInstance(OrthancPluginCreateInstanceResult& result /*out*/,
                                        const char* hashPatient,
                                        const char* hashStudy,
                                        const char* hashSeries,
                                        const char* hashInstance)
  {
    std::optional<bsoncxx::document::value> instance = LookupByPublicId(hashInstance, OrthancPluginResourceType_Instance);

    if (instance)
    {
      result.isNewInstance = false;
      result.instanceId = MongoDBToolbox::GetInteger(instance->view(), "internalId");
      return;
    }

    /**
     * Each missing level is created by one upsert, from the patient
     * down, so that no write can fail halfway on a duplicate: without
     * a transaction, a retry could not tell which resources the failed
     * attempt had created, and Orthanc would miss their "New*" changes.
     **/
    std::vector<int64_t> levels;
    MongoDBPatientRecycling recycling(manager_);

    result.isNewPatient = FindOrCreate(levels, hashPatient, hashInstance);
    result.patientId = levels[0];

    if (result.isNewPatient)
    {
      recycling.AddPatient(result.patientId);
    }

    result.isNewStudy = FindOrCreate(levels, hashStudy, hashInstance);
    result.studyId = levels[1];

    result.isNewSeries = FindOrCreate(levels, hashSeries, hashInstance);
    result.seriesId = levels[2];

    result.isNewInstance = FindOrCreate(levels, hashInstance, hashInstance);
    result.instanceId = levels[3];

    MongoDBStatisticsValues change;
    change.counts_[OrthancPluginResourceType_Patient] = (result.isNewPatient ? 1 : 0);
    change.counts_[OrthancPluginResourceType_Study] = (result.isNewStudy ? 1 : 0);
    change.counts_[OrthancPluginResourceType_Series] = (result.isNewSeries ? 1 : 0);
    change.counts_[OrthancPluginResourceType_Instance] = (result.isNewInstance ? 1 : 0);
    MongoDBStatistics(manager_).RecordChange(change);

    if (!result.isNewInstance)
    {
      // Another call stored the same instance meanwhile, and updates the ancestors
      return;
    }

    // Each ancestor lists the new resources among its descendants
    const int64_t ancestors = database_.GetCollection(COLLECTION).UpdateMany(
      make_document(kvp("internalId", make_document(kvp("$in", make_array(
                                                          result.patientId, result.studyId, result.seriesId))))),
      make_document(kvp("$addToSet", make_document(kvp("0", result.patientId),
                                                    kvp("1", result.studyId),
                                                    kvp("2", result.seriesId),
                                                    kvp("3", result.instanceId)))));

    /**
     * Without transactions, "DeleteResource()" can delete an ancestor
     * that "FindOrCreate()" has found, before it lists the instance. The
     * instance would be left under a deleted resource. If the ancestor
     * is deleted after it lists the instance, "SweepAfterDelete()" finds
     * the instance instead. Nothing refers to the new resources yet, so
     * Orthanc can retry the whole store, which re-creates the hierarchy.
     **/
    if (ancestors != 3 &&
        !database_.HasTransactions())
    {
      RemoveCreatedInstance(result);
      throw Orthanc::OrthancException(Orthanc::ErrorCode_DatabaseCannotSerialize,
                                      "An ancestor of the new instance was deleted meanwhile");
    }

    if (!result.isNewPatient)
    {
      recycling.TagMostRecentPatient(result.patientId);
    }
  }


  void MongoDBResources::AttachChild(int64_t parent,
                                     int64_t child)
  {
    MongoDBCollection collection = database_.GetCollection(COLLECTION);

    const bsoncxx::document::value parentDoc = GetByInternalId(parent);
    const bsoncxx::document::value childDoc = GetByInternalId(child);

    const int32_t parentLevel = MongoDBToolbox::GetInt32(parentDoc.view(), "resourceType");
    const int32_t childLevel = MongoDBToolbox::GetInt32(childDoc.view(), "resourceType");

    if (childLevel != parentLevel + 1)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    // The child and its descendants inherit the ancestors of the parent...
    {
      bsoncxx::builder::basic::document addToSet;
      for (int32_t level = 0; level <= parentLevel; level++)
      {
        const std::string key = GetLevelKey(level);
        addToSet.append(kvp(key, make_document(kvp("$each", parentDoc.view()[key].get_array().value))));
      }

      collection.UpdateMany(make_document(kvp(GetLevelKey(childLevel), child)),
                            make_document(kvp("$addToSet", addToSet.extract())));
    }

    // ... and the parent and its ancestors inherit the child and its descendants
    {
      bsoncxx::builder::basic::document addToSet;
      for (int32_t level = childLevel; level <= 3; level++)
      {
        const std::string key = GetLevelKey(level);
        addToSet.append(kvp(key, make_document(kvp("$each", childDoc.view()[key].get_array().value))));
      }

      collection.UpdateMany(make_document(kvp(GetLevelKey(parentLevel), parent),
                                          kvp("resourceType", make_document(kvp("$lte", parentLevel)))),
                            make_document(kvp("$addToSet", addToSet.extract())));
    }

    collection.UpdateOne(make_document(kvp("internalId", child)),
                         make_document(kvp("$set", make_document(kvp("parentId", parent)))));
  }


  void MongoDBResources::SweepAfterDelete(IDatabaseBackendOutput& output,
                                          std::list<int64_t>& ids)
  {
    MongoDBCollection collection = database_.GetCollection(COLLECTION);

    // 1. The resources created meanwhile under the deleted ones, and their descendants
    std::list<int64_t> late;
    MongoDBStatisticsValues change;

    {
      std::list<int64_t> frontier = ids;

      while (!frontier.empty())
      {
        MongoDBCollection::Documents children;
        collection.Find(children, make_document(
                          kvp("parentId", make_document(kvp("$in", MongoDBToolbox::ToArray(frontier))))));

        frontier.clear();
        for (size_t i = 0; i < children.size(); i++)
        {
          // Only one of the concurrent deletions takes each resource, and signals it
          std::optional<bsoncxx::document::value> taken =
            collection.FindOneAndDelete(make_document(kvp("_id", children[i].view()["_id"].get_value())));

          if (taken)
          {
            const int64_t id = MongoDBToolbox::GetInteger(taken->view(), "internalId");
            frontier.push_back(id);
            late.push_back(id);
            change.counts_[GetType(taken->view())]--;
            output.SignalDeletedResource(MongoDBToolbox::GetString(taken->view(), "publicId"),
                                         GetType(taken->view()));
          }
        }
      }
    }

    if (!late.empty())
    {
      MongoDBStatistics(manager_).RecordChange(change);
      MongoDBChanges(manager_).DeleteForResources(late);
      ids.insert(ids.end(), late.begin(), late.end());
    }

    // 2. What a store added to the resources since step 3 of "DeleteResource()"
    MongoDBAttachments(manager_).SweepForResources(output, ids);
    MongoDBMetadata(manager_).DeleteForResources(ids);
    MongoDBLabels(manager_).DeleteForResources(ids);
    MongoDBMainDicomTags(manager_).DeleteForResources(ids);
  }


  void MongoDBResources::RemoveCreatedInstance(const OrthancPluginCreateInstanceResult& created)
  {
    MongoDBCollection collection = database_.GetCollection(COLLECTION);

    const int64_t ids[4] = { created.patientId, created.studyId, created.seriesId, created.instanceId };
    const bool isNew[4] = { created.isNewPatient != 0, created.isNewStudy != 0, created.isNewSeries != 0, true };

    // The highest ancestor that was deleted meanwhile
    int32_t deleted = 3;

    {
      MongoDBCollection::Documents remaining;
      mongocxx::options::find options;
      options.projection(make_document(kvp("_id", 0), kvp("internalId", 1)));
      collection.Find(remaining, make_document(kvp("internalId", make_document(kvp("$in", make_array(ids[0], ids[1], ids[2]))))),
                      options);

      std::set<int64_t> found;
      for (size_t i = 0; i < remaining.size(); i++)
      {
        found.insert(MongoDBToolbox::GetInteger(remaining[i].view(), "internalId"));
      }

      for (int32_t level = 2; level >= 0; level--)
      {
        if (found.find(ids[level]) == found.end())
        {
          deleted = level;
        }
      }
    }

    // The remaining ancestors don't list the new resources anymore
    bsoncxx::builder::basic::document pull;
    for (int32_t level = 1; level <= 3; level++)
    {
      if (isNew[level])
      {
        pull.append(kvp(GetLevelKey(level), ids[level]));
      }
    }

    collection.UpdateMany(make_document(kvp("internalId", make_document(kvp("$in", make_array(ids[0], ids[1], ids[2]))))),
                          make_document(kvp("$pull", pull.extract())));

    /**
     * From the instance up, the resources that this call created, and
     * those under the deleted ancestor, as nothing else would delete
     * them. One that has children is kept: the stores that created
     * them fail as well, and the last one removes it.
     **/
    MongoDBStatisticsValues change;
    std::list<int64_t> removed;

    for (int32_t level = 3; level >= 0; level--)
    {
      if (level < 3 &&
          (!(isNew[level] || level > deleted) ||
           collection.Exists(make_document(kvp("parentId", ids[level])))))
      {
        break;
      }

      // "SweepAfterDelete()" or another store may have taken it, and counted it, already
      if (collection.FindOneAndDelete(make_document(kvp("internalId", ids[level]))))
      {
        change.counts_[level]--;
        removed.push_back(ids[level]);
      }
    }

    if (!removed.empty())
    {
      MongoDBStatistics(manager_).RecordChange(change);
      MongoDBMetadata(manager_).DeleteForResources(removed);
      MongoDBLabels(manager_).DeleteForResources(removed);
      MongoDBMainDicomTags(manager_).DeleteForResources(removed);
      MongoDBPatientRecycling(manager_).DeleteForPatients(removed);
    }
  }


  void MongoDBResources::DeleteResource(IDatabaseBackendOutput& output,
                                        int64_t id)
  {
    MongoDBCollection collection = database_.GetCollection(COLLECTION);

    MongoDBCollection::Documents deleted;
    deleted.push_back(GetByInternalId(id));

    // 1. The descendants, one level at a time
    {
      std::list<int64_t> frontier;
      frontier.push_back(id);

      while (!frontier.empty())
      {
        MongoDBCollection::Documents children;
        collection.Find(children, make_document(
                          kvp("parentId", make_document(kvp("$in", MongoDBToolbox::ToArray(frontier))))));

        frontier.clear();
        for (size_t i = 0; i < children.size(); i++)
        {
          frontier.push_back(MongoDBToolbox::GetInteger(children[i].view(), "internalId"));
          deleted.push_back(children[i]);
        }
      }
    }

    std::list<int64_t> ids;
    for (size_t i = 0; i < deleted.size(); i++)
    {
      ids.push_back(MongoDBToolbox::GetInteger(deleted[i].view(), "internalId"));
    }

    // 2. The ancestors that are left without children are deleted as
    //    well, as by the "ResourceDeleted" trigger of the SQL plugins
    std::optional<bsoncxx::document::value> remainingAncestor;

    {
      int64_t parent;
      bsoncxx::document::value current = deleted.front();

      while (MongoDBToolbox::LookupInteger(parent, current.view(), "parentId"))
      {
        std::optional<bsoncxx::document::value> parentDoc = LookupByInternalId(parent);
        if (!parentDoc)
        {
          break;  // Dangling parent, should not happen
        }

        if (collection.Exists(make_document(
                                kvp("parentId", parent),
                                kvp("internalId", make_document(kvp("$nin", MongoDBToolbox::ToArray(ids)))))))
        {
          remainingAncestor = parentDoc;
          break;
        }
        else
        {
          ids.push_back(parent);
          deleted.push_back(*parentDoc);
          current = *parentDoc;
        }
      }
    }

    // 3. Signal everything to Orthanc before deleting anything
    {
      MongoDBAttachments(manager_).DeleteForResources(output, ids);

      for (size_t i = 0; i < deleted.size(); i++)
      {
        output.SignalDeletedResource(MongoDBToolbox::GetString(deleted[i].view(), "publicId"),
                                     GetType(deleted[i].view()));
      }

      if (remainingAncestor)
      {
        output.SignalRemainingAncestor(MongoDBToolbox::GetString(remainingAncestor->view(), "publicId"),
                                       GetType(remainingAncestor->view()));
      }
    }

    // 4. Delete the resources and everything that refers to them
    MongoDBMetadata(manager_).DeleteForResources(ids);
    MongoDBLabels(manager_).DeleteForResources(ids);
    MongoDBMainDicomTags(manager_).DeleteForResources(ids);
    MongoDBChanges(manager_).DeleteForResources(ids);
    MongoDBPatientRecycling(manager_).DeleteForPatients(ids);

    {
      // One deletion per level counts what is actually deleted: without
      // transactions, "RemoveCreatedInstance()" of a store may have
      // taken, and counted, one of these resources meanwhile
      const bsoncxx::array::value deletedIds = MongoDBToolbox::ToArray(ids);

      MongoDBStatisticsValues change;
      for (int32_t level = 0; level <= 3; level++)
      {
        change.counts_[level] = -collection.DeleteMany(make_document(
                                                         kvp("internalId", make_document(kvp("$in", deletedIds.view()))),
                                                         kvp("resourceType", level)));
      }

      MongoDBStatistics(manager_).RecordChange(change);
    }

    if (!database_.HasTransactions())
    {
      SweepAfterDelete(output, ids);
    }

    // 5. The remaining ancestor and its own ancestors don't list the
    //    deleted resources among their descendants anymore
    if (remainingAncestor)
    {
      std::list<int64_t> ancestors;
      const int32_t level = MongoDBToolbox::GetInt32(remainingAncestor->view(), "resourceType");

      for (int32_t i = 0; i <= level; i++)
      {
        bsoncxx::document::element values = remainingAncestor->view()[GetLevelKey(i)];
        if (values && values.type() == bsoncxx::type::k_array)
        {
          for (const bsoncxx::array::element& value : values.get_array().value)
          {
            if (value.type() == bsoncxx::type::k_int64)
            {
              ancestors.push_back(value.get_int64().value);
            }
            else if (value.type() == bsoncxx::type::k_int32)
            {
              ancestors.push_back(value.get_int32().value);
            }
          }
        }
      }

      bsoncxx::builder::basic::document pull;
      for (int32_t i = level + 1; i <= 3; i++)
      {
        // "ids" also lists the resources of "SweepAfterDelete()"
        pull.append(kvp(GetLevelKey(i), make_document(kvp("$in", MongoDBToolbox::ToArray(ids)))));
      }

      collection.UpdateMany(make_document(kvp("internalId", make_document(kvp("$in", MongoDBToolbox::ToArray(ancestors))))),
                            make_document(kvp("$pull", pull.extract())));
    }
  }


  static void GetResources(MongoDBCollection& collection,
                           MongoDBCollection::Documents& target,
                           const bsoncxx::document::view_or_value& filter,
                           const char* field,
                           const mongocxx::options::find& options = mongocxx::options::find())
  {
    mongocxx::options::find projection(options);
    projection.projection(make_document(kvp(std::string(field), 1)));
    collection.Find(target, filter, projection);
  }


  void MongoDBResources::GetAllInternalIds(std::list<int64_t>& target /*out*/,
                                           OrthancPluginResourceType resourceType)
  {
    target.clear();

    MongoDBCollection collection = database_.GetCollection(COLLECTION);
    MongoDBCollection::Documents resources;
    GetResources(collection, resources, make_document(kvp("resourceType", static_cast<int32_t>(resourceType))), "internalId");

    for (size_t i = 0; i < resources.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetInteger(resources[i].view(), "internalId"));
    }
  }


  void MongoDBResources::GetAllPublicIds(std::list<std::string>& target /*out*/,
                                         OrthancPluginResourceType resourceType)
  {
    target.clear();

    MongoDBCollection collection = database_.GetCollection(COLLECTION);
    MongoDBCollection::Documents resources;
    GetResources(collection, resources, make_document(kvp("resourceType", static_cast<int32_t>(resourceType))), "publicId");

    for (size_t i = 0; i < resources.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetString(resources[i].view(), "publicId"));
    }
  }


  void MongoDBResources::GetAllPublicIds(std::list<std::string>& target /*out*/,
                                         OrthancPluginResourceType resourceType,
                                         int64_t since,
                                         uint32_t limit)
  {
    target.clear();

    // Sorted as in the SQL plugins, so that the pages are stable
    mongocxx::options::find options;
    options.sort(make_document(kvp("publicId", 1)));
    options.skip(since);
    options.limit(limit);

    MongoDBCollection collection = database_.GetCollection(COLLECTION);
    MongoDBCollection::Documents resources;
    GetResources(collection, resources, make_document(kvp("resourceType", static_cast<int32_t>(resourceType))), "publicId", options);

    for (size_t i = 0; i < resources.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetString(resources[i].view(), "publicId"));
    }
  }


  void MongoDBResources::GetChildrenInternalId(std::list<int64_t>& target /*out*/,
                                               int64_t id)
  {
    target.clear();

    MongoDBCollection collection = database_.GetCollection(COLLECTION);
    MongoDBCollection::Documents children;
    GetResources(collection, children, make_document(kvp("parentId", id)), "internalId");

    for (size_t i = 0; i < children.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetInteger(children[i].view(), "internalId"));
    }
  }


  void MongoDBResources::GetChildrenPublicId(std::list<std::string>& target /*out*/,
                                             int64_t id)
  {
    target.clear();

    MongoDBCollection collection = database_.GetCollection(COLLECTION);
    MongoDBCollection::Documents children;
    GetResources(collection, children, make_document(kvp("parentId", id)), "publicId");

    for (size_t i = 0; i < children.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetString(children[i].view(), "publicId"));
    }
  }


  void MongoDBResources::GetChildrenMetadata(std::list<std::string>& target /*out*/,
                                             int64_t resourceId,
                                             int32_t metadata)
  {
    std::list<int64_t> children;
    GetChildrenInternalId(children, resourceId);
    MongoDBMetadata(manager_).GetMetadataOfResources(target, children, metadata);
  }


  std::string MongoDBResources::GetPublicId(int64_t resourceId)
  {
    return MongoDBToolbox::GetString(GetByInternalId(resourceId).view(), "publicId");
  }


  uint64_t MongoDBResources::GetResourcesCount(OrthancPluginResourceType resourceType)
  {
    return static_cast<uint64_t>(database_.GetCollection(COLLECTION).CountDocuments(
                                   make_document(kvp("resourceType", static_cast<int32_t>(resourceType)))));
  }


  uint64_t MongoDBResources::GetAllResourcesCount()
  {
    return static_cast<uint64_t>(database_.GetCollection(COLLECTION).CountDocuments(make_document()));
  }


  OrthancPluginResourceType MongoDBResources::GetResourceType(int64_t resourceId)
  {
    return GetType(GetByInternalId(resourceId).view());
  }


  bool MongoDBResources::IsExistingResource(int64_t internalId)
  {
    return database_.GetCollection(COLLECTION).Exists(make_document(kvp("internalId", internalId)));
  }


  bool MongoDBResources::LookupParent(int64_t& parentId /*out*/,
                                      int64_t resourceId)
  {
    std::optional<bsoncxx::document::value> resource = LookupByInternalId(resourceId);
    return (resource && MongoDBToolbox::LookupInteger(parentId, resource->view(), "parentId"));
  }


  bool MongoDBResources::LookupResource(int64_t& id /*out*/,
                                        OrthancPluginResourceType& type /*out*/,
                                        const char* publicId)
  {
    std::optional<bsoncxx::document::value> resource =
      database_.GetCollection(COLLECTION).FindOne(make_document(kvp("publicId", publicId)));

    if (resource)
    {
      id = MongoDBToolbox::GetInteger(resource->view(), "internalId");
      type = GetType(resource->view());
      return true;
    }
    else
    {
      return false;
    }
  }


  bool MongoDBResources::LookupResourceAndParent(int64_t& id /*out*/,
                                                 OrthancPluginResourceType& type /*out*/,
                                                 std::string& parentPublicId /*out*/,
                                                 const char* publicId)
  {
    mongocxx::pipeline pipeline;
    pipeline.match(make_document(kvp("publicId", publicId)));
    pipeline.limit(1);
    pipeline.lookup(make_document(kvp("from", COLLECTION),
                                  kvp("localField", "parentId"),
                                  kvp("foreignField", "internalId"),
                                  kvp("as", "parent")));

    MongoDBCollection::Documents result;
    database_.GetCollection(COLLECTION).Aggregate(result, pipeline);

    if (result.empty())
    {
      return false;
    }

    const bsoncxx::document::view resource = result.front().view();
    id = MongoDBToolbox::GetInteger(resource, "internalId");
    type = GetType(resource);

    bsoncxx::document::element parent = resource["parent"];
    if (parent.type() == bsoncxx::type::k_array &&
        !parent.get_array().value.empty())
    {
      parentPublicId = MongoDBToolbox::GetString(parent.get_array().value[0].get_document().value, "publicId");
    }
    else
    {
      parentPublicId.clear();
    }

    return true;
  }
}
