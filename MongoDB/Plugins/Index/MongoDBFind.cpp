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


#include "MongoDBFind.h"

#include "MongoDBLabels.h"
#include "MongoDBLookup.h"
#include "MongoDBMainDicomTags.h"
#include "MongoDBResources.h"
#include "../../../Framework/MongoDB/MongoDBToolbox.h"

#include <OrthancException.h>

#include <algorithm>
#include <cassert>
#include <memory>
#include <set>
#include <vector>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_array;
  using bsoncxx::builder::basic::make_document;


  MongoDBFind::MongoDBFind(DatabaseManager& manager) :
    manager_(manager),
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


#if ORTHANC_PLUGINS_HAS_INTEGRATED_FIND == 1

  namespace Messages = Orthanc::DatabasePluginMessages;

  static const char* const METADATA = "Metadata";
  static const char* const ATTACHED_FILES = "AttachedFiles";


  // Names of the fields added by the pipeline (bsoncxx rejects
  // "const char*" variables as keys)
  static const std::string FIELD_TAGS = "tags";
  static const std::string FIELD_METADATA = "metadata";
  static const std::string FIELD_ATTACHMENTS = "attachments";
  static const std::string FIELD_LABELS = "labels";
  static const std::string FIELD_PARENT = "parent";
  static const std::string FIELD_INSTANCE_ID = "instanceId";
  static const std::string FIELD_INSTANCE = "instance";
  static const std::string FIELD_INSTANCE_METADATA = "instanceMetadata";
  static const std::string FIELD_INSTANCE_ATTACHMENTS = "instanceAttachments";


  // The level array "0" to "3" of a resource (cf. "GetLevelKey()" in "MongoDBLookup.cpp")
  static std::string GetLevelKey(int32_t level)
  {
    return std::to_string(level);
  }


  static std::string GetAncestorTagsField(int32_t level)
  {
    return "ancestorTags" + std::to_string(level);
  }


  static std::string GetAncestorMetadataField(int32_t level)
  {
    return "ancestorMetadata" + std::to_string(level);
  }


  static std::string GetChildrenIdsField(int32_t level)
  {
    return "childrenIds" + std::to_string(level);
  }


  static std::string GetChildrenCountField(int32_t level)
  {
    return "childrenCount" + std::to_string(level);
  }


  static std::string GetChildrenTagsField(int32_t level)
  {
    return "childrenTags" + std::to_string(level);
  }


  static std::string GetChildrenMetadataField(int32_t level)
  {
    return "childrenMetadata" + std::to_string(level);
  }


  static int32_t ConvertLevel(Messages::ResourceType level)
  {
    switch (level)
    {
      case Messages::RESOURCE_PATIENT:
        return OrthancPluginResourceType_Patient;

      case Messages::RESOURCE_STUDY:
        return OrthancPluginResourceType_Study;

      case Messages::RESOURCE_SERIES:
        return OrthancPluginResourceType_Series;

      case Messages::RESOURCE_INSTANCE:
        return OrthancPluginResourceType_Instance;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  // The specification of the ancestors of a level (patient, study or series)
  static const Messages::Find_Request_ParentSpecification& GetAncestorSpecification(const Messages::Find_Request& request,
                                                                                     int32_t level)
  {
    switch (level)
    {
      case OrthancPluginResourceType_Patient:
        return request.parent_patient();

      case OrthancPluginResourceType_Study:
        return request.parent_study();

      case OrthancPluginResourceType_Series:
        return request.parent_series();

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  // The specification of the descendants of a level (studies, series or instances)
  static const Messages::Find_Request_ChildrenSpecification& GetChildrenSpecification(const Messages::Find_Request& request,
                                                                                      int32_t level)
  {
    switch (level)
    {
      case OrthancPluginResourceType_Study:
        return request.children_studies();

      case OrthancPluginResourceType_Series:
        return request.children_series();

      case OrthancPluginResourceType_Instance:
        return request.children_instances();

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  static bool IsChildrenRequested(const Messages::Find_Request_ChildrenSpecification& specification)
  {
    return (specification.retrieve_identifiers() ||
            specification.retrieve_count() ||
            specification.retrieve_main_dicom_tags_size() > 0 ||
            specification.retrieve_metadata_size() > 0);
  }


  static Messages::Find_Response_ResourceContent& GetResourceContent(Messages::Find_Response& response,
                                                                     int32_t level)
  {
    switch (level)
    {
      case OrthancPluginResourceType_Patient:
        return *response.mutable_patient_content();

      case OrthancPluginResourceType_Study:
        return *response.mutable_study_content();

      case OrthancPluginResourceType_Series:
        return *response.mutable_series_content();

      case OrthancPluginResourceType_Instance:
        return *response.mutable_instance_content();

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  static Messages::Find_Response_ChildrenContent& GetChildrenContent(Messages::Find_Response& response,
                                                                     int32_t level)
  {
    switch (level)
    {
      case OrthancPluginResourceType_Study:
        return *response.mutable_children_studies_content();

      case OrthancPluginResourceType_Series:
        return *response.mutable_children_series_content();

      case OrthancPluginResourceType_Instance:
        return *response.mutable_children_instances_content();

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  static DatabaseConstraint* CreateMetadataConstraint(const Messages::DatabaseMetadataConstraint& constraint)
  {
    ConstraintType type;

    switch (constraint.type())
    {
      case Messages::CONSTRAINT_EQUAL:
        type = ConstraintType_Equal;
        break;

      case Messages::CONSTRAINT_SMALLER_OR_EQUAL:
        type = ConstraintType_SmallerOrEqual;
        break;

      case Messages::CONSTRAINT_GREATER_OR_EQUAL:
        type = ConstraintType_GreaterOrEqual;
        break;

      case Messages::CONSTRAINT_WILDCARD:
        type = ConstraintType_Wildcard;
        break;

      case Messages::CONSTRAINT_LIST:
        type = ConstraintType_List;
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    const std::vector<std::string> values(constraint.values().begin(), constraint.values().end());

    if (type != ConstraintType_List &&
        values.size() != 1)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    // The level and the tag are unused: "MongoDBLookup" puts the metadata on the query level
    return new DatabaseConstraint(Orthanc::ResourceType_Patient, Orthanc::DicomTag(0, 0), false, type, values,
                                  constraint.is_case_sensitive(), constraint.is_mandatory());
  }


  static LabelsConstraint ConvertLabelsConstraint(Messages::LabelsConstraintType constraint)
  {
    switch (constraint)
    {
      case Messages::LABELS_CONSTRAINT_ALL:
        return LabelsConstraint_All;

      case Messages::LABELS_CONSTRAINT_ANY:
        return LabelsConstraint_Any;

      case Messages::LABELS_CONSTRAINT_NONE:
        return LabelsConstraint_None;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  /**
   * The lowest level whose Orthanc ID is given, as
   * "OrthancIdentifiers::DetectLevel()" in the core. Returns "false"
   * if the request has no Orthanc ID.
   **/
  static bool GetIdentifiedResource(int32_t& level,
                                    std::string& publicId,
                                    const Messages::Find_Request& request)
  {
    if (!request.orthanc_id_instance().empty())
    {
      level = OrthancPluginResourceType_Instance;
      publicId = request.orthanc_id_instance();
    }
    else if (!request.orthanc_id_series().empty())
    {
      level = OrthancPluginResourceType_Series;
      publicId = request.orthanc_id_series();
    }
    else if (!request.orthanc_id_study().empty())
    {
      level = OrthancPluginResourceType_Study;
      publicId = request.orthanc_id_study();
    }
    else if (!request.orthanc_id_patient().empty())
    {
      level = OrthancPluginResourceType_Patient;
      publicId = request.orthanc_id_patient();
    }
    else
    {
      return false;
    }

    return true;
  }


  // The level array that lists the resource of level "tagLevel" that is the query resource or its ancestor
  static std::string GetLocalField(int32_t tagLevel,
                                   int32_t queryLevel)
  {
    return (tagLevel == queryLevel ? "internalId" : GetLevelKey(tagLevel));
  }


  // The level of the tag an ordering reads, which is the query level or one of its ancestors
  static int32_t GetOrderingLevel(const Messages::Find_Request_Ordering& ordering,
                                  int32_t queryLevel)
  {
    if (ordering.key_type() == Messages::ORDERING_KEY_TYPE_METADATA)
    {
      return queryLevel;
    }

    int32_t level = ConvertLevel(ordering.tag_level());

    if (level == OrthancPluginResourceType_Patient &&
        queryLevel == OrthancPluginResourceType_Study)
    {
      level = OrthancPluginResourceType_Study;  // The patient tags are copied at the study level, as in SQL
    }

    if (level > queryLevel)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented, "Ordering on a tag of a child level");
    }

    return level;
  }


  static void AppendLookup(bsoncxx::builder::basic::array& stages,
                           const char* from,
                           const std::string& localField,
                           const std::string& foreignField,
                           bsoncxx::array::value pipeline,
                           const std::string& as)
  {
    stages.append(make_document(kvp("$lookup", make_document(
      kvp("from", from),
      kvp("localField", localField),
      kvp("foreignField", foreignField),
      kvp("pipeline", std::move(pipeline)),
      kvp("as", as)))));
  }


  static bsoncxx::array::value CreateTagsPipeline(const bsoncxx::document::view& filter)
  {
    return make_array(make_document(kvp("$match", filter)),
                      make_document(kvp("$project", make_document(
                        kvp("_id", 0), kvp("tagGroup", 1), kvp("tagElement", 1), kvp("value", 1)))));
  }


  static bsoncxx::array::value CreateMetadataPipeline(const bsoncxx::document::view& filter)
  {
    return make_array(make_document(kvp("$match", filter)),
                      make_document(kvp("$project", make_document(
                        kvp("_id", 0), kvp("type", 1), kvp("value", 1), kvp("revision", 1)))));
  }


  static bsoncxx::array::value CreateAttachmentsPipeline()
  {
    return make_array(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("id", 0)))));
  }


  static bsoncxx::array::value CreatePublicIdPipeline()
  {
    return make_array(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("publicId", 1)))));
  }


  static uint64_t GetLastOfPage(const Messages::Find_Request& request)
  {
    return request.limits().since() + request.limits().count();
  }


  /**
   * The number of entries of the index that an ordering scan reads at
   * most: if fewer than one resource in 10 passes the filters, the
   * scan gives up, and the request runs without it.
   **/
  static int64_t GetOrderingScanLimit(const Messages::Find_Request& request)
  {
    return static_cast<int64_t>(std::max<uint64_t>(1000, 10 * GetLastOfPage(request)));
  }


  /**
   * The candidates of the request (step 1). If "bound" is not NULL,
   * the first ordering key must also be at most (ascending order) or
   * at least (descending order) this value, which is preferred to
   * start the pipeline. If "allowOrderingScan" is true, the
   * candidates may come in the order of the first ordering key, or of
   * the public IDs if the request has no ordering, cf. "orderingScan"
   * in "MongoDBLookup::AppendCandidates()".
   **/
  static bool AppendCandidates(std::string& collection,
                               bsoncxx::builder::basic::array& stages,
                               bool& orderingScan,
                               DatabaseManager& manager,
                               MongoDBDatabase& database,
                               const Messages::Find_Request& request,
                               const std::set<std::string>& fields,
                               const std::string* bound,
                               bool allowOrderingScan)
  {
    const int32_t level = ConvertLevel(request.level());

    DatabaseConstraints tags;
    for (int i = 0; i < request.dicom_tag_constraints_size(); i++)
    {
      tags.AddConstraint(new DatabaseConstraint(request.dicom_tag_constraints(i)));
    }

    std::vector<std::unique_ptr<DatabaseConstraint> > metadata;
    metadata.reserve(request.metadata_constraints_size());

    MongoDBLookup::CandidateRequest candidates(level, tags);
    candidates.fields_ = fields;
    candidates.distinct_ = true;  // Only applies to a driver, never to an ordering scan (which streams)

    for (int i = 0; i < request.metadata_constraints_size(); i++)
    {
      metadata.emplace_back(CreateMetadataConstraint(request.metadata_constraints(i)));
      candidates.metadata_.push_back(std::make_pair(request.metadata_constraints(i).metadata(), metadata.back().get()));
    }

    candidates.labels_.insert(request.labels().begin(), request.labels().end());
    candidates.labelsConstraint_ = ConvertLabelsConstraint(request.labels_constraint());

    int32_t identifiedLevel;
    std::string identifiedPublicId;
    if (GetIdentifiedResource(identifiedLevel, identifiedPublicId, request))
    {
      mongocxx::options::find options;
      options.projection(make_document(kvp("_id", 0), kvp("internalId", 1), kvp("resourceType", 1)));

      std::optional<bsoncxx::document::value> identified =
        database.GetCollection(MongoDBResources::COLLECTION).FindOne(make_document(kvp("publicId", identifiedPublicId)), options);

      if (!identified ||
          MongoDBToolbox::GetInteger(identified->view(), "resourceType") != identifiedLevel)
      {
        return false;
      }

      candidates.hasIdentified_ = true;
      candidates.identifiedLevel_ = identifiedLevel;
      candidates.identifiedId_ = MongoDBToolbox::GetInteger(identified->view(), "internalId");
    }

    for (int i = 0; i < request.ordering_size(); i++)
    {
      const int32_t orderingLevel = GetOrderingLevel(request.ordering(i), level);
      if (orderingLevel != level)
      {
        candidates.fields_.insert(GetLevelKey(orderingLevel));
      }
    }

    if (allowOrderingScan)
    {
      candidates.hasOrderingScan_ = true;
      candidates.orderingScanLimit_ = GetOrderingScanLimit(request);
    }

    if (allowOrderingScan &&
        request.ordering_size() == 0)
    {
      candidates.orderingOnPublicId_ = true;
      candidates.orderingDirection_ = 1;
    }
    else if (bound != NULL ||
             allowOrderingScan)
    {
      const Messages::Find_Request_Ordering& ordering = request.ordering(0);
      assert(ordering.key_type() == Messages::ORDERING_KEY_TYPE_DICOM_TAG &&
             GetOrderingLevel(ordering, level) == level);

      const bool descending = (ordering.direction() == Messages::ORDERING_DIRECTION_DESC);

      if (bound != NULL)
      {
        DatabaseConstraint* constraint = new DatabaseConstraint(
          MessagesToolbox::Convert(static_cast<OrthancPluginResourceType>(level)),
          Orthanc::DicomTag(ordering.tag_group(), ordering.tag_element()), ordering.is_identifier_tag(),
          descending ? ConstraintType_GreaterOrEqual : ConstraintType_SmallerOrEqual,
          std::vector<std::string>(1, *bound), true /* case sensitive, as the index */, true /* mandatory */);
        tags.AddConstraint(constraint);
        candidates.preferredDriver_ = constraint;
      }

      if (allowOrderingScan)
      {
        candidates.orderingIsIdentifier_ = ordering.is_identifier_tag();
        candidates.orderingGroup_ = static_cast<uint16_t>(ordering.tag_group());
        candidates.orderingElement_ = static_cast<uint16_t>(ordering.tag_element());
        candidates.orderingDirection_ = (descending ? -1 : 1);
      }
    }

    return MongoDBLookup(manager).AppendCandidates(collection, stages, orderingScan, candidates);
  }


  // Step 2: sorts the candidates
  static void AppendOrdering(bsoncxx::builder::basic::array& stages,
                             const Messages::Find_Request& request)
  {
    const int32_t level = ConvertLevel(request.level());

    /**
     * The ordering keys. A missing value comes last in both
     * directions ("NULLS LAST"), and a value that cannot be cast is
     * treated as missing, instead of failing as in SQL.
     **/
    bsoncxx::builder::basic::document keys, missing, order;

    for (int i = 0; i < request.ordering_size(); i++)
    {
      const Messages::Find_Request_Ordering& ordering = request.ordering(i);
      const std::string values = "orderValues" + std::to_string(i);
      const std::string key = "orderKey" + std::to_string(i);
      const std::string isMissing = "orderMissing" + std::to_string(i);

      bsoncxx::builder::basic::document filter;
      const char* from;

      if (ordering.key_type() == Messages::ORDERING_KEY_TYPE_METADATA)
      {
        from = METADATA;
        filter.append(kvp("type", ordering.metadata()));
      }
      else if (ordering.key_type() == Messages::ORDERING_KEY_TYPE_DICOM_TAG)
      {
        from = (ordering.is_identifier_tag() ?
                MongoDBMainDicomTags::DICOM_IDENTIFIERS :
                MongoDBMainDicomTags::MAIN_DICOM_TAGS);
        filter.append(kvp("tagGroup", static_cast<int32_t>(ordering.tag_group())),
                      kvp("tagElement", static_cast<int32_t>(ordering.tag_element())));
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }

      AppendLookup(stages, from, GetLocalField(GetOrderingLevel(ordering, level), level), "id",
                   make_array(make_document(kvp("$match", filter.extract())),
                              make_document(kvp("$limit", 1)),
                              make_document(kvp("$project", make_document(kvp("_id", 0), kvp("value", 1))))),
                   values);

      bsoncxx::document::value value = make_document(kvp("$first", "$" + values + ".value"));

      switch (ordering.cast())
      {
        case Messages::ORDERING_CAST_STRING:
          keys.append(kvp(key, value.view()));
          break;

        case Messages::ORDERING_CAST_INT:
        case Messages::ORDERING_CAST_FLOAT:
          keys.append(kvp(key, make_document(kvp("$convert", make_document(
            kvp("input", make_document(kvp("$trim", make_document(kvp("input", value.view()))))),
            kvp("to", ordering.cast() == Messages::ORDERING_CAST_INT ? "long" : "double"),
            kvp("onError", bsoncxx::types::b_null()),
            kvp("onNull", bsoncxx::types::b_null()))))));
          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }

      // "$ifNull" because "$eq" does not consider a missing field as null
      missing.append(kvp(isMissing, make_document(kvp("$cond", make_array(
        make_document(kvp("$eq", make_array(make_document(kvp("$ifNull", make_array("$" + key, bsoncxx::types::b_null()))),
                                            bsoncxx::types::b_null()))),
        1, 0)))));

      order.append(kvp(isMissing, 1),
                   kvp(key, ordering.direction() == Messages::ORDERING_DIRECTION_DESC ? -1 : 1));
    }

    if (request.ordering_size() > 0)
    {
      stages.append(make_document(kvp("$addFields", keys.extract())));
      stages.append(make_document(kvp("$addFields", missing.extract())));
    }

    // The public ID makes the order total, so that "since" is repeatable
    order.append(kvp("publicId", 1));
    stages.append(make_document(kvp("$sort", order.extract())));
  }


  static void AppendLimits(bsoncxx::builder::basic::array& stages,
                           const Messages::Find_Request& request)
  {
    if (request.has_limits())
    {
      if (request.limits().since() > 0)
      {
        stages.append(make_document(kvp("$skip", static_cast<int64_t>(request.limits().since()))));
      }

      if (request.limits().count() > 0)
      {
        stages.append(make_document(kvp("$limit", static_cast<int64_t>(request.limits().count()))));
      }
    }

  }


  /**
   * Whether an ordering scan may give a page of the resources of a
   * level faster: a page, no Orthanc ID and no label to have (both
   * select few resources), and either no ordering (the scan is then on
   * the public IDs), or an ordering by a string tag of this level (for
   * "FindOrderingBound()"). "MongoDBLookup" then only scans if no
   * mandatory tag is selective. This is the study list of OE2: "the
   * latest 100 studies", possibly of a modality or a patient name.
   **/
  static bool IsOrderingScanApplicable(const Messages::Find_Request& request)
  {
    if (!request.has_limits() ||
        request.limits().count() == 0 ||
        !request.orthanc_id_patient().empty() ||
        !request.orthanc_id_study().empty() ||
        !request.orthanc_id_series().empty() ||
        !request.orthanc_id_instance().empty() ||
        (request.labels_size() > 0 &&
         request.labels_constraint() != Messages::LABELS_CONSTRAINT_NONE))
    {
      return false;
    }
    else if (request.ordering_size() == 0)
    {
      return true;
    }
    else
    {
      const Messages::Find_Request_Ordering& ordering = request.ordering(0);
      return (ordering.key_type() == Messages::ORDERING_KEY_TYPE_DICOM_TAG &&
              ordering.cast() == Messages::ORDERING_CAST_STRING &&
              GetOrderingLevel(ordering, ConvertLevel(request.level())) == ConvertLevel(request.level()));
    }
  }


  /**
   * The value of the first ordering key of the last resource of the
   * page (the "since + count"-th one), read from the index on the
   * values of the tag. The page is then among the resources whose key
   * is at most (or at least) this value, which the index also gives:
   * the ordering reads the keys of about one page of resources, and
   * not of all the resources of the level. Returns "false" if fewer
   * resources have this tag, i.e. if the page reaches the resources
   * without it, which come last.
   **/
  static bool FindOrderingBound(std::string& bound,
                                DatabaseManager& manager,
                                MongoDBDatabase& database,
                                const Messages::Find_Request& request)
  {
    std::string collection;
    bsoncxx::builder::basic::array stages;
    bool orderingScan;

    if (!AppendCandidates(collection, stages, orderingScan, manager, database, request, std::set<std::string>(), NULL, true) ||
        !orderingScan)
    {
      return false;
    }

    if (GetLastOfPage(request) > 1)
    {
      stages.append(make_document(kvp("$skip", static_cast<int64_t>(GetLastOfPage(request) - 1))));
    }

    stages.append(make_document(kvp("$limit", 1)));
    stages.append(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("internalId", 1)))));

    mongocxx::pipeline pipeline;
    pipeline.append_stages(stages.extract());

    MongoDBCollection::Documents last;
    database.GetCollection(collection).Aggregate(last, pipeline);

    if (last.empty())
    {
      return false;
    }

    const Messages::Find_Request_Ordering& ordering = request.ordering(0);

    mongocxx::options::find options;
    options.projection(make_document(kvp("_id", 0), kvp("value", 1)));

    std::optional<bsoncxx::document::value> tag = database.GetCollection(
      ordering.is_identifier_tag() ? MongoDBMainDicomTags::DICOM_IDENTIFIERS : MongoDBMainDicomTags::MAIN_DICOM_TAGS).FindOne(
        make_document(kvp("id", MongoDBToolbox::GetInteger(last.front().view(), "internalId")),
                      kvp("tagGroup", static_cast<int32_t>(ordering.tag_group())),
                      kvp("tagElement", static_cast<int32_t>(ordering.tag_element()))), options);

    return (tag &&
            MongoDBToolbox::LookupString(bound, tag->view(), "value"));
  }


  /**
   * Appends to "stages" the pipeline on "collection" that yields the
   * selected resources, in order and limited, with their "internalId",
   * their "publicId" and the "fields". Returns "false" if no resource
   * can match. "sort" is false for a count, whose order is irrelevant.
   *
   * "optimistic" tells whether an ordering scan was used (only if
   * "allowScan"): its page is right if it is full. Otherwise, some
   * resource has two documents for the ordering tag (the bound of
   * "FindOrderingBound()" is then too tight), or the scan limit was
   * reached before the page (on the public IDs): the caller must run
   * the request again without scan.
   **/
  static bool AppendSelection(std::string& collection,
                              bsoncxx::builder::basic::array& stages,
                              bool& optimistic,
                              DatabaseManager& manager,
                              MongoDBDatabase& database,
                              const Messages::Find_Request& request,
                              const std::set<std::string>& fields,
                              bool sort,
                              bool allowScan)
  {
    const bool scan = (sort &&
                       allowScan &&
                       IsOrderingScanApplicable(request));

    std::string bound;
    const bool bounded = (scan &&
                          request.ordering_size() > 0 &&
                          FindOrderingBound(bound, manager, database, request));

    // Without ordering, the scan on the public IDs gives the page directly (they are unique)
    bool orderingScan;
    if (!AppendCandidates(collection, stages, orderingScan, manager, database, request, fields,
                          bounded ? &bound : NULL, scan && request.ordering_size() == 0))
    {
      return false;
    }

    optimistic = (bounded || orderingScan);

    // The scan on the public IDs gives the candidates in order already
    if (sort &&
        !orderingScan)
    {
      AppendOrdering(stages, request);
    }

    AppendLimits(stages, request);

    return true;
  }


  static bsoncxx::array::view GetArray(const bsoncxx::document::view& document,
                                       const std::string& key)
  {
    bsoncxx::document::element element = document[key];

    if (!element)
    {
      return bsoncxx::array::view();
    }
    else if (element.type() == bsoncxx::type::k_array)
    {
      return element.get_array().value;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_Database, "Field is not an array: " + key);
    }
  }


  static bsoncxx::document::view GetDocument(const bsoncxx::array::element& element)
  {
    if (element.type() == bsoncxx::type::k_document)
    {
      return element.get_document().value;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_Database, "Array item is not a document");
    }
  }


  template <typename Content>
  static void ReadTags(Content& content,
                       const bsoncxx::document::view& document,
                       const std::string& key)
  {
    bsoncxx::array::view tags = GetArray(document, key);

    for (bsoncxx::array::view::const_iterator it = tags.begin(); it != tags.end(); ++it)
    {
      const bsoncxx::document::view tag = GetDocument(*it);

      Messages::Find_Response_Tag* target = content.add_main_dicom_tags();
      target->set_group(static_cast<uint32_t>(MongoDBToolbox::GetInteger(tag, "tagGroup")));
      target->set_element(static_cast<uint32_t>(MongoDBToolbox::GetInteger(tag, "tagElement")));
      target->set_value(MongoDBToolbox::GetString(tag, "value"));
    }
  }


  static void ReadMetadata(Messages::Find_Response_Metadata& target,
                           const bsoncxx::document::view& metadata,
                           bool revision)
  {
    target.set_key(MongoDBToolbox::GetInt32(metadata, "type"));
    target.set_value(MongoDBToolbox::GetString(metadata, "value"));

    // The revision of the metadata of the children is unused, as of Orthanc 1.12.5
    target.set_revision(revision ? MongoDBToolbox::GetRevision(metadata) : 0);
  }


  template <typename Content>
  static void ReadMetadata(Content& content,
                           const bsoncxx::document::view& document,
                           const std::string& key,
                           bool revision)
  {
    bsoncxx::array::view metadata = GetArray(document, key);

    for (bsoncxx::array::view::const_iterator it = metadata.begin(); it != metadata.end(); ++it)
    {
      ReadMetadata(*content.add_metadata(), GetDocument(*it), revision);
    }
  }


  static void ReadAttachment(Messages::FileInfo& target,
                             const bsoncxx::document::view& attachment)
  {
    target.set_uuid(MongoDBToolbox::GetString(attachment, "uuid"));
    target.set_content_type(MongoDBToolbox::GetInt32(attachment, "fileType"));
    target.set_uncompressed_size(static_cast<uint64_t>(MongoDBToolbox::GetInteger(attachment, "uncompressedSize")));
    target.set_uncompressed_hash(MongoDBToolbox::GetString(attachment, "uncompressedHash"));
    target.set_compression_type(MongoDBToolbox::GetInt32(attachment, "compressionType"));
    target.set_compressed_size(static_cast<uint64_t>(MongoDBToolbox::GetInteger(attachment, "compressedSize")));
    target.set_compressed_hash(MongoDBToolbox::GetString(attachment, "compressedHash"));

    if (attachment["customData"])
    {
      target.set_custom_data(MongoDBToolbox::GetBinary(attachment, "customData"));
    }
  }


  void MongoDBFind::ExecuteFind(Messages::TransactionResponse& response,
                                const Messages::Find_Request& request)
  {
    const int32_t level = ConvertLevel(request.level());

    const bool oneInstance = (request.retrieve_one_instance_metadata_and_attachments() &&
                              level != OrthancPluginResourceType_Instance);

    // 1. The level arrays that lead to the requested ancestors and descendants

    std::set<std::string> levelArrays;

    for (int32_t ancestor = 0; ancestor < level; ancestor++)
    {
      const Messages::Find_Request_ParentSpecification& specification = GetAncestorSpecification(request, ancestor);
      if (specification.retrieve_main_dicom_tags() ||
          specification.retrieve_metadata() ||
          (request.retrieve_parent_identifier() && ancestor == level - 1))
      {
        levelArrays.insert(GetLevelKey(ancestor));
      }
    }

    for (int32_t child = level + 1; child <= OrthancPluginResourceType_Instance; child++)
    {
      if (IsChildrenRequested(GetChildrenSpecification(request, child)))
      {
        levelArrays.insert(GetLevelKey(child));
      }
    }

    if (oneInstance)
    {
      levelArrays.insert(GetLevelKey(OrthancPluginResourceType_Instance));
    }

    MongoDBCollection::Documents resources;

    for (bool allowScan = true; ; )
    {
      // 2. The page of selected resources

      std::string collection;
      bsoncxx::builder::basic::array stages;
      bool optimistic;

      if (!AppendSelection(collection, stages, optimistic, manager_, database_, request, levelArrays, true, allowScan))
      {
        return;
      }

      /**
       * 3. Their content. This projection drops the fields of the
       * selection, and keeps the level arrays before they are used as
       * "localField" (cf. the MongoDB bug in "GetLevelKey()").
       **/

      {
        bsoncxx::builder::basic::document projection;
        projection.append(kvp("_id", 0), kvp("internalId", 1), kvp("publicId", 1));

        for (std::set<std::string>::const_iterator it = levelArrays.begin(); it != levelArrays.end(); ++it)
        {
          projection.append(kvp(*it, 1));
        }

        stages.append(make_document(kvp("$project", projection.extract())));
      }

      bsoncxx::builder::basic::document result;
      result.append(kvp("_id", 0), kvp("internalId", 1), kvp("publicId", 1));

      if (request.retrieve_main_dicom_tags())
      {
        AppendLookup(stages, MongoDBMainDicomTags::MAIN_DICOM_TAGS, "internalId", "id", CreateTagsPipeline(make_document()), FIELD_TAGS);
        result.append(kvp(FIELD_TAGS, 1));
      }

      if (request.retrieve_metadata())
      {
        AppendLookup(stages, METADATA, "internalId", "id", CreateMetadataPipeline(make_document()), FIELD_METADATA);
        result.append(kvp(FIELD_METADATA, 1));
      }

      if (request.retrieve_attachments())
      {
        AppendLookup(stages, ATTACHED_FILES, "internalId", "id", CreateAttachmentsPipeline(), FIELD_ATTACHMENTS);
        result.append(kvp(FIELD_ATTACHMENTS, 1));
      }

      if (request.retrieve_labels())
      {
        AppendLookup(stages, MongoDBLabels::COLLECTION, "internalId", "id",
                     make_array(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("label", 1))))),
                     FIELD_LABELS);
        result.append(kvp(FIELD_LABELS, 1));
      }

      for (int32_t ancestor = 0; ancestor < level; ancestor++)
      {
        const Messages::Find_Request_ParentSpecification& specification = GetAncestorSpecification(request, ancestor);

        if (specification.retrieve_main_dicom_tags())
        {
          AppendLookup(stages, MongoDBMainDicomTags::MAIN_DICOM_TAGS, GetLevelKey(ancestor), "id",
                       CreateTagsPipeline(make_document()), GetAncestorTagsField(ancestor));
          result.append(kvp(GetAncestorTagsField(ancestor), 1));
        }

        if (specification.retrieve_metadata())
        {
          AppendLookup(stages, METADATA, GetLevelKey(ancestor), "id",
                       CreateMetadataPipeline(make_document()), GetAncestorMetadataField(ancestor));
          result.append(kvp(GetAncestorMetadataField(ancestor), 1));
        }
      }

      if (request.retrieve_parent_identifier() &&
          level != OrthancPluginResourceType_Patient)
      {
        AppendLookup(stages, MongoDBResources::COLLECTION, GetLevelKey(level - 1), "internalId",
                     CreatePublicIdPipeline(), FIELD_PARENT);
        result.append(kvp(FIELD_PARENT, 1));
      }

      for (int32_t child = level + 1; child <= OrthancPluginResourceType_Instance; child++)
      {
        const Messages::Find_Request_ChildrenSpecification& specification = GetChildrenSpecification(request, child);

        if (specification.retrieve_identifiers())
        {
          AppendLookup(stages, MongoDBResources::COLLECTION, GetLevelKey(child), "internalId",
                       CreatePublicIdPipeline(), GetChildrenIdsField(child));
          result.append(kvp(GetChildrenIdsField(child), 1));
        }
        else if (specification.retrieve_count())
        {
          // The level array lists the descendants of the level
          result.append(kvp(GetChildrenCountField(child), make_document(
            kvp("$size", make_document(kvp("$ifNull", make_array("$" + GetLevelKey(child), make_array())))))));
        }

        if (specification.retrieve_main_dicom_tags_size() > 0)
        {
          bsoncxx::builder::basic::array tags;
          for (int i = 0; i < specification.retrieve_main_dicom_tags_size(); i++)
          {
            tags.append(make_document(kvp("tagGroup", static_cast<int32_t>(specification.retrieve_main_dicom_tags(i).group())),
                                      kvp("tagElement", static_cast<int32_t>(specification.retrieve_main_dicom_tags(i).element()))));
          }

          AppendLookup(stages, MongoDBMainDicomTags::MAIN_DICOM_TAGS, GetLevelKey(child), "id",
                       CreateTagsPipeline(make_document(kvp("$or", tags.extract()))), GetChildrenTagsField(child));
          result.append(kvp(GetChildrenTagsField(child), 1));
        }

        if (specification.retrieve_metadata_size() > 0)
        {
          bsoncxx::builder::basic::array types;
          for (int i = 0; i < specification.retrieve_metadata_size(); i++)
          {
            types.append(specification.retrieve_metadata(i));
          }

          AppendLookup(stages, METADATA, GetLevelKey(child), "id",
                       CreateMetadataPipeline(make_document(kvp("type", make_document(kvp("$in", types.extract()))))),
                       GetChildrenMetadataField(child));
          result.append(kvp(GetChildrenMetadataField(child), 1));
        }
      }

      if (oneInstance)
      {
        // The first instance of the level array is the first one that was stored
        stages.append(make_document(kvp("$addFields", make_document(
          kvp(FIELD_INSTANCE_ID, make_document(kvp("$first", "$" + GetLevelKey(OrthancPluginResourceType_Instance))))))));

        AppendLookup(stages, MongoDBResources::COLLECTION, FIELD_INSTANCE_ID, "internalId",
                     CreatePublicIdPipeline(), FIELD_INSTANCE);
        AppendLookup(stages, METADATA, FIELD_INSTANCE_ID, "id",
                     CreateMetadataPipeline(make_document()), FIELD_INSTANCE_METADATA);
        AppendLookup(stages, ATTACHED_FILES, FIELD_INSTANCE_ID, "id",
                     CreateAttachmentsPipeline(), FIELD_INSTANCE_ATTACHMENTS);

        result.append(kvp(FIELD_INSTANCE, 1),
                      kvp(FIELD_INSTANCE_METADATA, 1),
                      kvp(FIELD_INSTANCE_ATTACHMENTS, 1));
      }

      stages.append(make_document(kvp("$project", result.extract())));

      mongocxx::pipeline pipeline;
      pipeline.append_stages(stages.extract());

      mongocxx::options::aggregate options;
      options.allow_disk_use(true);

      database_.GetCollection(collection).Aggregate(resources, pipeline, options);

      if (optimistic &&
          resources.size() < request.limits().count())
      {
        // Cf. "AppendSelection()"
        resources.clear();
        allowScan = false;
      }
      else
      {
        break;
      }
    }

    // 4. The answers, in the order of the pipeline

    for (size_t i = 0; i < resources.size(); i++)
    {
      const bsoncxx::document::view resource = resources[i].view();

      Messages::Find_Response& target = *response.add_find();
      target.set_internal_id(MongoDBToolbox::GetInteger(resource, "internalId"));
      target.set_public_id(MongoDBToolbox::GetString(resource, "publicId"));

      if (request.retrieve_main_dicom_tags())
      {
        ReadTags(GetResourceContent(target, level), resource, FIELD_TAGS);
      }

      if (request.retrieve_metadata())
      {
        ReadMetadata(GetResourceContent(target, level), resource, FIELD_METADATA, true);
      }

      if (request.retrieve_attachments())
      {
        bsoncxx::array::view attachments = GetArray(resource, FIELD_ATTACHMENTS);
        for (bsoncxx::array::view::const_iterator it = attachments.begin(); it != attachments.end(); ++it)
        {
          const bsoncxx::document::view attachment = GetDocument(*it);
          ReadAttachment(*target.add_attachments(), attachment);
          target.add_attachments_revisions(MongoDBToolbox::GetRevision(attachment));
        }
      }

      if (request.retrieve_labels())
      {
        bsoncxx::array::view labels = GetArray(resource, FIELD_LABELS);
        for (bsoncxx::array::view::const_iterator it = labels.begin(); it != labels.end(); ++it)
        {
          target.add_labels(MongoDBToolbox::GetString(GetDocument(*it), "label"));
        }
      }

      for (int32_t ancestor = 0; ancestor < level; ancestor++)
      {
        const Messages::Find_Request_ParentSpecification& specification = GetAncestorSpecification(request, ancestor);

        if (specification.retrieve_main_dicom_tags())
        {
          ReadTags(GetResourceContent(target, ancestor), resource, GetAncestorTagsField(ancestor));
        }

        if (specification.retrieve_metadata())
        {
          ReadMetadata(GetResourceContent(target, ancestor), resource, GetAncestorMetadataField(ancestor), true);
        }
      }

      if (request.retrieve_parent_identifier() &&
          level != OrthancPluginResourceType_Patient)
      {
        bsoncxx::array::view parent = GetArray(resource, FIELD_PARENT);
        if (!parent.empty())
        {
          target.set_parent_public_id(MongoDBToolbox::GetString(GetDocument(*parent.begin()), "publicId"));
        }
      }

      for (int32_t child = level + 1; child <= OrthancPluginResourceType_Instance; child++)
      {
        const Messages::Find_Request_ChildrenSpecification& specification = GetChildrenSpecification(request, child);

        if (!IsChildrenRequested(specification))
        {
          continue;
        }

        Messages::Find_Response_ChildrenContent& content = GetChildrenContent(target, child);

        if (specification.retrieve_identifiers())
        {
          bsoncxx::array::view children = GetArray(resource, GetChildrenIdsField(child));
          for (bsoncxx::array::view::const_iterator it = children.begin(); it != children.end(); ++it)
          {
            content.add_identifiers(MongoDBToolbox::GetString(GetDocument(*it), "publicId"));
          }

          content.set_count(content.identifiers_size());
        }
        else if (specification.retrieve_count())
        {
          content.set_count(static_cast<uint64_t>(MongoDBToolbox::GetInteger(resource, GetChildrenCountField(child))));
        }

        if (specification.retrieve_main_dicom_tags_size() > 0)
        {
          ReadTags(content, resource, GetChildrenTagsField(child));
        }

        if (specification.retrieve_metadata_size() > 0)
        {
          ReadMetadata(content, resource, GetChildrenMetadataField(child), false);
        }
      }

      if (oneInstance)
      {
        bsoncxx::array::view instance = GetArray(resource, FIELD_INSTANCE);
        if (!instance.empty())
        {
          target.set_one_instance_public_id(MongoDBToolbox::GetString(GetDocument(*instance.begin()), "publicId"));

          bsoncxx::array::view metadata = GetArray(resource, FIELD_INSTANCE_METADATA);
          for (bsoncxx::array::view::const_iterator it = metadata.begin(); it != metadata.end(); ++it)
          {
            ReadMetadata(*target.add_one_instance_metadata(), GetDocument(*it), true);
          }

          bsoncxx::array::view attachments = GetArray(resource, FIELD_INSTANCE_ATTACHMENTS);
          for (bsoncxx::array::view::const_iterator it = attachments.begin(); it != attachments.end(); ++it)
          {
            ReadAttachment(*target.add_one_instance_attachments(), GetDocument(*it));
          }
        }
      }
    }
  }


  void MongoDBFind::ExecuteCount(Messages::TransactionResponse& response,
                                 const Messages::Find_Request& request)
  {
    std::string collection;
    bsoncxx::builder::basic::array stages;

    int64_t count = 0;
    bool optimistic;

    // As in SQL, the count applies the limits of the request, but not its ordering
    if (AppendSelection(collection, stages, optimistic, manager_, database_, request, std::set<std::string>(), false, false))
    {
      stages.append(make_document(kvp("$count", "count")));

      mongocxx::pipeline pipeline;
      pipeline.append_stages(stages.extract());

      mongocxx::options::aggregate options;
      options.allow_disk_use(true);

      MongoDBCollection::Documents result;
      database_.GetCollection(collection).Aggregate(result, pipeline, options);

      if (!result.empty())
      {
        count = MongoDBToolbox::GetInteger(result.front().view(), "count");
      }
    }

    response.mutable_count_resources()->set_count(static_cast<uint64_t>(count));
  }

#endif
}
