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


#include "MongoDBLookup.h"

#include "MongoDBLabels.h"

#include "MongoDBMainDicomTags.h"
#include "MongoDBResources.h"
#include "../../../Framework/MongoDB/MongoDBToolbox.h"
#include "../../../Framework/Plugins/MessagesToolbox.h"

#include <OrthancException.h>

#include <algorithm>
#include <tuple>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_array;
  using bsoncxx::builder::basic::make_document;


  /**
   * The driver is chosen by counting the matching tags of each
   * mandatory group, up to this number. Such a count only reads the
   * keys of the index on (tagGroup, tagElement, value).
   **/
  static const int64_t PROBE_LIMIT = 1000;


  /**
   * If several groups reach the limit of their count, they are counted
   * again with a limit 10 times higher, up to this one: 100,000 index
   * keys are counted in about 30 ms. Otherwise, a one-month "StudyDate"
   * range (1,650 studies) and "Modality = MR" (450,000 series) would
   * both count 1,000, and the equality would drive (18 s on 100,000
   * patients).
   **/
  static const int64_t PROBE_LIMIT_MAX = 100000;


  // Names of the temporary fields of the pipeline (bsoncxx rejects
  // "const char*" variables as keys)
  static const std::string FIELD_MATCH = "match";
  static const std::string FIELD_DESCENDANT = "descendant";
  static const std::string FIELD_RESOURCE = "resource";
  static const std::string FIELD_FIRST_INSTANCE = "firstInstance";
  static const std::string FIELD_INSTANCE = "instance";
  static const std::string FIELD_LABELS = "labels";


  namespace
  {
    /**
     * All the constraints on one tag of one level. They apply to the
     * same tag document, so they are evaluated by one "$match".
     **/
    struct TagGroup
    {
      int32_t                                level_;
      int32_t                                metadata_;    // The metadata type, or -1 for a tag
      bool                                   isIdentifier_;
      bool                                   isMandatory_;
      uint16_t                               group_;
      uint16_t                               element_;
      unsigned int                           cost_;
      bool                                   bounded_;     // Whether a condition bounds the index scan on "value"
      bool                                   preferred_;   // Whether to start the pipeline from this group if possible
      bool                                   counted_;     // Whether "estimate_" was counted
      int64_t                                estimate_;    // Number of matching tags, at most the probe limit
      int64_t                                candidates_;  // Number of resources of the query level it yields (see "CountCandidates()")
      std::vector<bsoncxx::document::value>  conditions_;  // Empty if the tag must only exist
    };
  }


  /**
   * The level array "0" to "3" of a resource. MongoDB 7.0 rejects a
   * "$lookup" whose "localField" is such a numeric field name ("FieldPath
   * cannot be constructed with empty string") if a later stage needs
   * specific fields ("$project", "$group", "$replaceWith") and no earlier
   * inclusion "$project" kept the level array. The candidates are always
   * projected before any "$lookup" on a level array, and this projection
   * must keep the level arrays that are joined on.
   **/
  static std::string GetLevelKey(int32_t level)
  {
    return std::to_string(level);
  }


  static std::string GetLevelArray(int32_t level)
  {
    return "$" + std::to_string(level);
  }


  static const char* GetCollection(const TagGroup& group)
  {
    if (group.metadata_ >= 0)
    {
      return "Metadata";
    }
    else
    {
      return (group.isIdentifier_ ?
              MongoDBMainDicomTags::DICOM_IDENTIFIERS :
              MongoDBMainDicomTags::MAIN_DICOM_TAGS);
    }
  }


  // Exact match, ignoring the case
  static std::string CreateEqualityRegex(const std::string& value)
  {
    return MongoDBMainDicomTags::ConvertWildcardToRegex(value, false);
  }


  /**
   * The index bounds of an exact match that ignores the case: the case
   * variants of an ASCII value lie between its upper-case form (the
   * lowest, as "A" < "a") and its lower-case form. The regular
   * expression still checks the value. This does not apply to a value
   * with a non-ASCII character, nor with "k" or "s", which the "i"
   * option also matches with the Kelvin sign (U+212A) and the long s
   * (U+017F), outside these bounds.
   **/
  static bool GetCaseInsensitiveBounds(std::string& lowest,
                                       std::string& highest,
                                       const std::string& value)
  {
    lowest.clear();
    highest.clear();

    for (size_t i = 0; i < value.size(); i++)
    {
      const unsigned char c = static_cast<unsigned char>(value[i]);
      if (c >= 0x80 ||
          c == 'k' || c == 'K' ||
          c == 's' || c == 'S')
      {
        return false;
      }

      lowest.push_back(static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
      highest.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }

    return true;
  }


  /**
   * Comparison that ignores the case, as "lower(value) <= lower(x)" in
   * the SQL plugins. "$literal" prevents a value starting with "$"
   * from being read as a field path.
   **/
  static bsoncxx::document::value CreateCaseInsensitiveComparison(const std::string& op,
                                                                  const std::string& value)
  {
    return make_document(kvp("$expr", make_document(
      kvp(op, make_array(make_document(kvp("$toLower", "$value")),
                         make_document(kvp("$toLower", make_document(kvp("$literal", value)))))))));
  }


  bool MongoDBLookup::AppendValueConditions(std::vector<bsoncxx::document::value>& target,
                                            const DatabaseConstraint& constraint)
  {
    // NB: "bsoncxx::types::b_regex" only holds views, so it must be
    // built in the expression that appends it, while the pattern is
    // still alive
    switch (constraint.GetConstraintType())
    {
      case ConstraintType_Equal:
        if (constraint.IsCaseSensitive())
        {
          target.push_back(make_document(kvp("value", constraint.GetSingleValue())));
        }
        else
        {
          std::string lowest, highest;
          if (GetCaseInsensitiveBounds(lowest, highest, constraint.GetSingleValue()))
          {
            target.push_back(make_document(kvp("value", make_document(kvp("$gte", lowest), kvp("$lte", highest)))));
          }

          target.push_back(make_document(kvp("value", bsoncxx::types::b_regex{
                  CreateEqualityRegex(constraint.GetSingleValue()), "i"})));
        }
        return true;

      case ConstraintType_SmallerOrEqual:
        if (constraint.IsCaseSensitive())
        {
          target.push_back(make_document(kvp("value", make_document(kvp("$lte", constraint.GetSingleValue())))));
        }
        else
        {
          target.push_back(CreateCaseInsensitiveComparison("$lte", constraint.GetSingleValue()));
        }
        return true;

      case ConstraintType_GreaterOrEqual:
        if (constraint.IsCaseSensitive())
        {
          target.push_back(make_document(kvp("value", make_document(kvp("$gte", constraint.GetSingleValue())))));
        }
        else
        {
          target.push_back(CreateCaseInsensitiveComparison("$gte", constraint.GetSingleValue()));
        }
        return true;

      case ConstraintType_List:
      {
        bsoncxx::builder::basic::array values;
        for (size_t i = 0; i < constraint.GetValuesCount(); i++)
        {
          if (constraint.IsCaseSensitive())
          {
            values.append(constraint.GetValue(i));
          }
          else
          {
            values.append(bsoncxx::types::b_regex{CreateEqualityRegex(constraint.GetValue(i)), "i"});
          }
        }

        target.push_back(make_document(kvp("value", make_document(kvp("$in", values.extract())))));
        return true;
      }

      case ConstraintType_Wildcard:
        if (constraint.GetSingleValue() == "*")
        {
          return false;
        }
        else
        {
          target.push_back(make_document(kvp("value", bsoncxx::types::b_regex{
                  MongoDBMainDicomTags::ConvertWildcardToRegex(constraint.GetSingleValue()),
                  constraint.IsCaseSensitive() ? "" : "i"})));
          return true;
        }

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  unsigned int MongoDBLookup::GetSelectivityCost(const DatabaseConstraint& constraint)
  {
    // A regular expression with the "i" option, or "$expr", cannot use
    // the bounds of the index on "value": all the keys of the tag are
    // scanned
    const bool caseSensitive = constraint.IsCaseSensitive();

    switch (constraint.GetConstraintType())
    {
      case ConstraintType_Equal:
        return caseSensitive ? 0 : 3;

      case ConstraintType_List:
        return caseSensitive ? 1 : 4;

      case ConstraintType_Wildcard:
      {
        const std::string& value = constraint.GetSingleValue();
        if (value == "*")
        {
          return 9;  // Only checks that the tag exists
        }
        else if (caseSensitive &&
                 value[0] != '*' &&
                 value[0] != '?')
        {
          return 2;  // Anchored prefix, that bounds the index scan
        }
        else
        {
          return 5;
        }
      }

      case ConstraintType_SmallerOrEqual:
      case ConstraintType_GreaterOrEqual:
        return caseSensitive ? 6 : 7;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  bool MongoDBLookup::IsIndexBounded(const DatabaseConstraint& constraint)
  {
    // Neither a regular expression with the "i" option nor "$expr"
    // bounds the scan of the index on "value" (except the exact match
    // of "GetCaseInsensitiveBounds()"), and "*" has no condition on
    // "value"
    if (!constraint.IsCaseSensitive())
    {
      std::string lowest, highest;
      return (constraint.GetConstraintType() == ConstraintType_Equal &&
              GetCaseInsensitiveBounds(lowest, highest, constraint.GetSingleValue()));
    }
    else if (constraint.GetConstraintType() == ConstraintType_Wildcard)
    {
      const std::string& value = constraint.GetSingleValue();
      return (value[0] != '*' && value[0] != '?');
    }
    else
    {
      return true;
    }
  }


  /**
   * The filter on the tag documents of a group. If "failing" is true,
   * it matches the tag documents that do not satisfy the group.
   **/
  static bsoncxx::document::value CreateTagFilter(const TagGroup& group,
                                                  bool failing)
  {
    bsoncxx::builder::basic::document filter;
    if (group.metadata_ >= 0)
    {
      filter.append(kvp("type", group.metadata_));
    }
    else
    {
      filter.append(kvp("tagGroup", group.group_),
                    kvp("tagElement", group.element_));
    }

    if (!group.conditions_.empty())
    {
      bsoncxx::builder::basic::array conditions;
      for (size_t i = 0; i < group.conditions_.size(); i++)
      {
        conditions.append(group.conditions_[i].view());
      }

      if (failing)
      {
        filter.append(kvp("$nor", make_array(make_document(kvp("$and", conditions.extract())))));
      }
      else
      {
        filter.append(kvp("$and", conditions.extract()));
      }
    }

    return filter.extract();
  }


  /**
   * Keeps the documents whose resource in "localField" satisfies the
   * group. A mandatory group needs a matching tag document. A group
   * that is not mandatory rejects the resource only if its tag exists
   * and does not match ("value IS NULL OR ..." in SQL).
   **/
  static void AppendTagGroupFilter(bsoncxx::builder::basic::array& stages,
                                   const TagGroup& group,
                                   const std::string& localField)
  {
    const bool failing = !group.isMandatory_;

    stages.append(make_document(kvp("$lookup", make_document(
      kvp("from", GetCollection(group)),
      kvp("localField", localField),
      kvp("foreignField", "id"),
      kvp("pipeline", make_array(make_document(kvp("$match", CreateTagFilter(group, failing))),
                                 make_document(kvp("$limit", 1)),
                                 make_document(kvp("$project", make_document(kvp("_id", 1)))))),
      kvp("as", FIELD_MATCH)))));

    stages.append(make_document(kvp("$match", make_document(
      kvp(FIELD_MATCH, make_document(kvp(std::string(failing ? "$eq" : "$ne"), make_array())))))));
  }


  static void AppendDescendantFilter(bsoncxx::builder::basic::array& stages,
                                     const std::vector<int32_t>& levels,
                                     size_t index,
                                     const std::vector<const TagGroup*>& groups);


  /**
   * Pipeline run on the descendants of level "levels[index]": one of
   * them must satisfy all the groups of its level, and have itself a
   * descendant that satisfies the groups of the next lower levels.
   **/
  static bsoncxx::array::value CreateDescendantPipeline(const std::vector<int32_t>& levels,
                                                        size_t index,
                                                        const std::vector<const TagGroup*>& groups)
  {
    const int32_t level = levels[index];
    const bool hasNext = (index + 1 < levels.size());

    bsoncxx::builder::basic::document projection;
    projection.append(kvp("_id", 0), kvp("internalId", 1));
    if (hasNext)
    {
      projection.append(kvp(GetLevelKey(levels[index + 1]), 1));
    }

    bsoncxx::builder::basic::array stages;
    stages.append(make_document(kvp("$project", projection.extract())));

    for (size_t i = 0; i < groups.size(); i++)
    {
      if (groups[i]->level_ == level)
      {
        AppendTagGroupFilter(stages, *groups[i], "internalId");
      }
    }

    if (hasNext)
    {
      AppendDescendantFilter(stages, levels, index + 1, groups);
    }

    stages.append(make_document(kvp("$limit", 1)));
    stages.append(make_document(kvp("$project", make_document(kvp("_id", 1)))));

    return stages.extract();
  }


  // Keeps the documents that have a descendant of level "levels[index]" satisfying the groups
  static void AppendDescendantFilter(bsoncxx::builder::basic::array& stages,
                                     const std::vector<int32_t>& levels,
                                     size_t index,
                                     const std::vector<const TagGroup*>& groups)
  {
    stages.append(make_document(kvp("$lookup", make_document(
      kvp("from", MongoDBResources::COLLECTION),
      kvp("localField", GetLevelKey(levels[index])),
      kvp("foreignField", "internalId"),
      kvp("pipeline", CreateDescendantPipeline(levels, index, groups)),
      kvp("as", FIELD_DESCENDANT)))));

    stages.append(make_document(kvp("$match", make_document(
      kvp(FIELD_DESCENDANT, make_document(kvp("$ne", make_array())))))));
  }


  static bsoncxx::array::value ToArray(const std::set<std::string>& values)
  {
    bsoncxx::builder::basic::array array;
    for (std::set<std::string>::const_iterator it = values.begin(); it != values.end(); ++it)
    {
      array.append(*it);
    }

    return array.extract();
  }


  /**
   * Keeps the resources whose labels satisfy the constraint, as the
   * "COUNT(1) FROM Labels" subquery of "ISqlLookupFormatter". Since
   * Orthanc 1.12.11, "None" without labels means "without any label".
   **/
  static void AppendLabelsFilter(bsoncxx::builder::basic::array& stages,
                                 const std::set<std::string>& labels,
                                 LabelsConstraint constraint)
  {
    bsoncxx::builder::basic::array pipeline;

    if (!labels.empty())
    {
      pipeline.append(make_document(kvp("$match", make_document(kvp("label", make_document(kvp("$in", ToArray(labels))))))));
    }

    if (constraint != LabelsConstraint_All)
    {
      pipeline.append(make_document(kvp("$limit", 1)));  // Only the existence matters
    }

    pipeline.append(make_document(kvp("$project", make_document(kvp("_id", 1)))));

    stages.append(make_document(kvp("$lookup", make_document(
      kvp("from", MongoDBLabels::COLLECTION),
      kvp("localField", "internalId"),
      kvp("foreignField", "id"),
      kvp("pipeline", pipeline.extract()),
      kvp("as", FIELD_LABELS)))));

    switch (constraint)
    {
      case LabelsConstraint_Any:
        stages.append(make_document(kvp("$match", make_document(kvp(FIELD_LABELS, make_document(kvp("$ne", make_array())))))));
        break;

      case LabelsConstraint_All:
        // (id, label) is unique, so there is one document per matching label
        stages.append(make_document(kvp("$match", make_document(
          kvp(FIELD_LABELS, make_document(kvp("$size", static_cast<int64_t>(labels.size()))))))));
        break;

      case LabelsConstraint_None:
        stages.append(make_document(kvp("$match", make_document(kvp(FIELD_LABELS, make_array())))));
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


  /**
   * Replaces each document by the resources of the query level whose
   * "foreignField" equals "localField" (or contains it, if it is one
   * of the level arrays)
   **/
  static void AppendCandidateJoin(bsoncxx::builder::basic::array& stages,
                                  const std::string& localField,
                                  const std::string& foreignField,
                                  int32_t queryLevel,
                                  const bsoncxx::document::view& candidateProjection)
  {
    stages.append(make_document(kvp("$lookup", make_document(
      kvp("from", MongoDBResources::COLLECTION),
      kvp("localField", localField),
      kvp("foreignField", foreignField),
      kvp("pipeline", make_array(make_document(kvp("$match", make_document(kvp("resourceType", queryLevel)))),
                                 make_document(kvp("$project", candidateProjection)))),
      kvp("as", FIELD_RESOURCE)))));

    stages.append(make_document(kvp("$unwind", "$" + FIELD_RESOURCE)));
    stages.append(make_document(kvp("$replaceWith", "$" + FIELD_RESOURCE)));
  }


  // Counts the matching tags of the group, up to "limit", through the index on (tagGroup, tagElement, value)
  static int64_t CountMatchingTags(MongoDBDatabase& database,
                                   const TagGroup& group,
                                   int64_t limit)
  {
    mongocxx::options::count options;
    options.limit(limit);
    return database.GetCollection(GetCollection(group)).CountDocuments(CreateTagFilter(group, false), options);
  }


  /**
   * Counts, up to "limit", the resources of the query level that list a
   * resource matching the group (of an ancestor level) in their level
   * array, among the first "probeLimit" matching tags. This is the join
   * that the group would start the pipeline with, through the index on
   * the level array and "resourceType".
   **/
  static int64_t CountCandidates(MongoDBDatabase& database,
                                 const TagGroup& group,
                                 int32_t queryLevel,
                                 int64_t probeLimit,
                                 int64_t limit)
  {
    bsoncxx::builder::basic::array stages;
    stages.append(make_document(kvp("$match", CreateTagFilter(group, false))));
    stages.append(make_document(kvp("$limit", probeLimit)));
    stages.append(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("id", 1)))));
    stages.append(make_document(kvp("$lookup", make_document(
      kvp("from", MongoDBResources::COLLECTION),
      kvp("localField", "id"),
      kvp("foreignField", GetLevelKey(group.level_)),
      kvp("pipeline", make_array(make_document(kvp("$match", make_document(kvp("resourceType", queryLevel)))),
                                 make_document(kvp("$limit", limit)),
                                 make_document(kvp("$project", make_document(kvp("_id", 1)))))),
      kvp("as", FIELD_RESOURCE)))));
    stages.append(make_document(kvp("$unwind", "$" + FIELD_RESOURCE)));
    stages.append(make_document(kvp("$limit", limit)));
    stages.append(make_document(kvp("$count", "count")));

    mongocxx::pipeline pipeline;
    pipeline.append_stages(stages.extract());

    MongoDBCollection::Documents result;
    database.GetCollection(GetCollection(group)).Aggregate(result, pipeline);

    return (result.empty() ? 0 : MongoDBToolbox::GetInteger(result[0].view(), "count"));
  }


  MongoDBLookup::MongoDBLookup(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  // Adds the constraint to its group (same level, same tag or metadata, same mandatory flag)
  static void AddToGroups(std::vector<TagGroup>& groups,
                          const DatabaseConstraint& constraint,
                          int32_t level,
                          int32_t metadata,
                          bool preferred)
  {
    std::vector<bsoncxx::document::value> conditions;
    if (!MongoDBLookup::AppendValueConditions(conditions, constraint) &&
        !constraint.IsMandatory())
    {
      return;  // Universal constraint on an optional tag
    }

    const bool isIdentifier = (metadata < 0 && constraint.IsIdentifier());
    const uint16_t tagGroup = (metadata < 0 ? constraint.GetTag().GetGroup() : 0);
    const uint16_t tagElement = (metadata < 0 ? constraint.GetTag().GetElement() : 0);
    const unsigned int cost = MongoDBLookup::GetSelectivityCost(constraint);

    TagGroup* group = NULL;
    for (size_t j = 0; j < groups.size(); j++)
    {
      if (groups[j].level_ == level &&
          groups[j].metadata_ == metadata &&
          groups[j].isIdentifier_ == isIdentifier &&
          groups[j].isMandatory_ == constraint.IsMandatory() &&
          groups[j].group_ == tagGroup &&
          groups[j].element_ == tagElement)
      {
        group = &groups[j];
        break;
      }
    }

    if (group == NULL)
    {
      groups.push_back(TagGroup{level, metadata, isIdentifier, constraint.IsMandatory(),
                                tagGroup, tagElement, cost, false, false, false, PROBE_LIMIT, PROBE_LIMIT, {}});
      group = &groups.back();
    }

    group->preferred_ = (group->preferred_ || preferred);

    group->cost_ = std::min(group->cost_, cost);
    group->bounded_ = (group->bounded_ || MongoDBLookup::IsIndexBounded(constraint));

    for (size_t j = 0; j < conditions.size(); j++)
    {
      group->conditions_.push_back(std::move(conditions[j]));
    }
  }


  bool MongoDBLookup::AppendCandidates(std::string& collection,
                                       bsoncxx::builder::basic::array& stages,
                                       const CandidateRequest& request)
  {
    bool orderingScan;
    return AppendCandidates(collection, stages, orderingScan, request);
  }


  bool MongoDBLookup::AppendCandidates(std::string& collection,
                                       bsoncxx::builder::basic::array& stages,
                                       bool& orderingScan,
                                       const CandidateRequest& request)
  {
    orderingScan = false;

    const int32_t level = request.level_;
    const std::set<std::string>& labels = request.labels_;
    const LabelsConstraint labelsConstraint = request.labelsConstraint_;

    const bool hasLabelsFilter = (!labels.empty() ||
                                  labelsConstraint == LabelsConstraint_None);

    // 1. Group the constraints by tag (or metadata) and level

    std::vector<TagGroup> groups;
    groups.reserve((request.tags_ == NULL ? 0 : request.tags_->GetSize()) + request.metadata_.size());

    if (request.tags_ != NULL)
    {
      for (size_t i = 0; i < request.tags_->GetSize(); i++)
      {
        const DatabaseConstraint& constraint = request.tags_->GetConstraint(i);
        AddToGroups(groups, constraint, static_cast<int32_t>(MessagesToolbox::ConvertToPlainC(constraint.GetLevel())), -1,
                    &constraint == request.preferredDriver_);
      }
    }

    for (size_t i = 0; i < request.metadata_.size(); i++)
    {
      AddToGroups(groups, *request.metadata_[i].second, level, request.metadata_[i].first, false);
    }

    /**
     * 2. Choose the driver among the mandatory groups: the fewest
     * matching tags, then the lowest static cost, then the identifiers
     * (normalized and fewer), then the query level (no fan-out), then
     * an ancestor level. A static cost alone is not enough: a one-week
     * "StudyDate" range is far more selective than "Modality = CT",
     * although an equality is usually more selective than a range.
     *
     * An identified resource is a better start than any tag: its level
     * array gives the candidates at once. The metadata have no index
     * on their values, so they never start the pipeline.
     *
     * A group of an ancestor level is compared by the candidates it
     * yields, not by its matching tags: one study gives all its
     * instances. For instance, DICOMweb resolves each instance it
     * retrieves by its study, series and SOP instance UIDs. If the SOP
     * instance UID is found in 4 copies of a study, the study (1 match)
     * would drive and yield its 1,000 instances, instead of the 4
     * instances of the SOP instance UID (33 ms instead of 3 ms).
     **/

    std::vector<TagGroup*> eligible;

    for (size_t i = 0; i < groups.size(); i++)
    {
      TagGroup& group = groups[i];
      if (!group.isMandatory_ ||
          group.metadata_ >= 0 ||
          request.hasIdentified_)
      {
        continue;
      }

      eligible.push_back(&group);

      /**
       * Counting is cheap if a condition bounds the index scan, or if
       * the tag must only exist (the count reaches the limit at once).
       * Otherwise, it could read all the index keys of the tag.
       **/
      if (group.bounded_ ||
          group.conditions_.empty())
      {
        group.estimate_ = CountMatchingTags(database_, group, PROBE_LIMIT);
        group.counted_ = true;

        if (group.estimate_ == 0)
        {
          return false;  // No resource can satisfy this group
        }
      }
    }

    /**
     * The ordering scan replaces a driver that reaches "PROBE_LIMIT"
     * (see below), so counting further would not change the plan.
     **/
    const bool orderingScanPossible = (request.hasOrderingScan_ &&
                                       !request.hasIdentified_ &&
                                       (labels.empty() || labelsConstraint == LabelsConstraint_None));

    /**
     * The candidates of each group, counted up to "probeLimit". If
     * several groups reach it and none is below, they cannot be told
     * apart: those that reached it are counted again with a higher
     * limit. For instance, "Modality = MR" (450,000 series) and a
     * one-month "StudyDate" range (1,650 studies, 10,500 series) need
     * a limit of 100,000.
     **/
    int64_t probeLimit = PROBE_LIMIT;

    for (;;)
    {
      int64_t bestOwnLevel = -1;  // Fewest matching tags of a group of the query level or below, -1 if none is counted

      for (size_t i = 0; i < eligible.size(); i++)
      {
        TagGroup& group = *eligible[i];

        if (!group.counted_)
        {
          // Unknown: as many as the most, as when all were counted up to "PROBE_LIMIT"
          group.estimate_ = probeLimit;
        }
        else if (group.level_ >= level &&
                 (bestOwnLevel == -1 || group.estimate_ < bestOwnLevel))
        {
          bestOwnLevel = group.estimate_;
        }

        // A resource of the query level has at most one matching descendant per matching tag
        group.candidates_ = group.estimate_;
      }

      for (size_t i = 0; i < eligible.size(); i++)
      {
        TagGroup& group = *eligible[i];

        /**
         * The candidates of an ancestor group are only counted if it has
         * at most as many matching tags as the best group of the query
         * level or below (otherwise, it has more candidates, if each
         * matching ancestor has a descendant). The count stops just above
         * that number, so it reads a few index keys, and a group with
         * more candidates cannot win on its static cost.
         **/
        if (group.level_ < level &&
            group.estimate_ < probeLimit &&
            bestOwnLevel != -1 &&
            group.estimate_ <= bestOwnLevel)
        {
          group.candidates_ = CountCandidates(database_, group, level, probeLimit, bestOwnLevel + 1);

          if (group.candidates_ == 0)
          {
            return false;  // No matching ancestor has a resource of the query level
          }
        }
      }

      size_t saturated = 0;
      bool below = false;  // Whether a counted group is below the limit, and thus better than those that reach it

      for (size_t i = 0; i < eligible.size(); i++)
      {
        if (eligible[i]->counted_)
        {
          if (eligible[i]->candidates_ >= probeLimit)
          {
            saturated++;
          }
          else
          {
            below = true;
          }
        }
      }

      if (below ||
          saturated < 2 ||
          orderingScanPossible ||
          probeLimit >= PROBE_LIMIT_MAX)
      {
        break;
      }

      const int64_t previous = probeLimit;
      probeLimit = std::min(probeLimit * 10, PROBE_LIMIT_MAX);

      for (size_t i = 0; i < eligible.size(); i++)
      {
        TagGroup& group = *eligible[i];

        if (group.counted_ &&
            group.estimate_ >= previous)
        {
          group.estimate_ = CountMatchingTags(database_, group, probeLimit);
        }
      }
    }

    const TagGroup* driver = NULL;

    for (size_t i = 0; i < eligible.size(); i++)
    {
      const TagGroup& group = *eligible[i];
      const int levelRank = (group.level_ == level ? 0 : (group.level_ < level ? 1 : 2));
      const int driverRank = (driver == NULL ? 0 :
                              (driver->level_ == level ? 0 : (driver->level_ < level ? 1 : 2)));

      if (driver == NULL ||
          std::make_tuple(!group.preferred_, group.candidates_, group.cost_, !group.isIdentifier_, levelRank) <
          std::make_tuple(!driver->preferred_, driver->candidates_, driver->cost_, !driver->isIdentifier_, driverRank))
      {
        driver = &group;
      }
    }

    /**
     * The ordering scan replaces a driver that matches too many tags
     * to be selective: its group becomes a filter. For instance, the
     * 100 latest CT studies are found among the first studies of the
     * index on "StudyDate", instead of joining all the CT series with
     * their study (and then reading the date of all these studies).
     **/
    const bool useOrderingScan = (request.hasOrderingScan_ &&
                                  !request.hasIdentified_ &&
                                  (labels.empty() || labelsConstraint == LabelsConstraint_None) &&
                                  (driver == NULL || driver->estimate_ >= PROBE_LIMIT));
    /**
     * A mandatory group on the tag of the ordering scan is part of the
     * scan, whose index bounds then skip the values it rejects (e.g. the
     * 100 latest studies of 2018 start at the end of 2018, and not at
     * the latest date).
     **/
    const TagGroup* scanGroup = NULL;

    if (useOrderingScan)
    {
      driver = NULL;

      for (size_t i = 0; i < groups.size() && !request.orderingOnPublicId_; i++)
      {
        if (groups[i].metadata_ < 0 &&
            groups[i].level_ == level &&
            groups[i].isMandatory_ &&
            groups[i].isIdentifier_ == request.orderingIsIdentifier_ &&
            groups[i].group_ == request.orderingGroup_ &&
            groups[i].element_ == request.orderingElement_)
        {
          scanGroup = &groups[i];
        }
      }
    }

    /**
     * The driver is fully checked when it yields the candidates, unless
     * it is on a lower level: the same descendant must then satisfy
     * the other groups of its level, so it is checked again with them.
     **/
    std::vector<const TagGroup*> sameOrAncestorGroups, descendantGroups;
    std::vector<int32_t> descendantLevels;

    for (size_t i = 0; i < groups.size(); i++)
    {
      if (groups[i].level_ > level)
      {
        descendantGroups.push_back(&groups[i]);
        descendantLevels.push_back(groups[i].level_);
      }
      else if (&groups[i] != driver &&
               &groups[i] != scanGroup)
      {
        sameOrAncestorGroups.push_back(&groups[i]);
      }
    }

    if (descendantGroups.size() == 1 &&
        descendantGroups[0] == driver)
    {
      /**
       * The driver is the only group of the lower levels: each
       * candidate was reached from a matching descendant, so checking
       * again would only repeat, per candidate, the scan of the driver
       * (e.g. 16 s instead of 0.2 s for "ModalitiesInStudy=CT").
       **/
      descendantGroups.clear();
      descendantLevels.clear();
    }

    std::sort(descendantLevels.begin(), descendantLevels.end());
    descendantLevels.erase(std::unique(descendantLevels.begin(), descendantLevels.end()), descendantLevels.end());

    // 3. The fields kept from the candidate resources

    bsoncxx::document::value candidateProjection = make_document();

    {
      bsoncxx::builder::basic::document projection;
      projection.append(kvp("_id", 0), kvp("internalId", 1), kvp("publicId", 1));

      std::set<std::string> fields = request.fields_;
      fields.erase("internalId");
      fields.erase("publicId");

      for (size_t i = 0; i < sameOrAncestorGroups.size(); i++)
      {
        if (sameOrAncestorGroups[i]->level_ < level)
        {
          fields.insert(GetLevelKey(sameOrAncestorGroups[i]->level_));
        }
      }

      if (!descendantLevels.empty())
      {
        fields.insert(GetLevelKey(descendantLevels.front()));
      }

      for (std::set<std::string>::const_iterator it = fields.begin(); it != fields.end(); ++it)
      {
        projection.append(kvp(*it, 1));
      }

      candidateProjection = projection.extract();
    }

    // 4. The candidate resources

    // Without a tag to start from, the labels are a better start than all the resources of the level
    const bool labelsDrive = (driver == NULL &&
                              !request.hasIdentified_ &&
                              !labels.empty() &&
                              labelsConstraint != LabelsConstraint_None);

    if (request.hasIdentified_)
    {
      /**
       * The level array of the identified resource lists it in its
       * ancestors, in its descendants and in itself, and it is indexed
       * with "resourceType"
       **/
      collection = MongoDBResources::COLLECTION;
      stages.append(make_document(kvp("$match", make_document(
        kvp("resourceType", level),
        kvp(GetLevelKey(request.identifiedLevel_), request.identifiedId_)))));
      stages.append(make_document(kvp("$project", candidateProjection.view())));
    }
    else if (labelsDrive)
    {
      collection = MongoDBLabels::COLLECTION;
      stages.append(make_document(kvp("$match", make_document(kvp("label", make_document(kvp("$in", ToArray(labels))))))));
      stages.append(make_document(kvp("$group", make_document(kvp("_id", "$id"), kvp("count", make_document(kvp("$sum", 1)))))));

      if (labelsConstraint == LabelsConstraint_All)
      {
        stages.append(make_document(kvp("$match", make_document(kvp("count", static_cast<int64_t>(labels.size()))))));
      }

      // The labels of the other levels are dropped by the join, which matches "resourceType"
      AppendCandidateJoin(stages, "_id", "internalId", level, candidateProjection.view());
    }
    else if (useOrderingScan)
    {
      /**
       * The index gives the entries in order, and the following stages
       * keep this order. Followed by "$limit", the pipeline stops once
       * it has enough candidates, instead of reading all the resources
       * of the level.
       **/
      orderingScan = true;

      if (request.orderingOnPublicId_)
      {
        // Index on (resourceType, publicId, internalId)
        collection = MongoDBResources::COLLECTION;
        stages.append(make_document(kvp("$match", make_document(kvp("resourceType", level)))));
        stages.append(make_document(kvp("$sort", make_document(kvp("publicId", request.orderingDirection_)))));

        if (request.orderingScanLimit_ > 0)
        {
          stages.append(make_document(kvp("$limit", request.orderingScanLimit_)));
        }

        stages.append(make_document(kvp("$project", candidateProjection.view())));
      }
      else
      {
        // Index on (tagGroup, tagElement, value). The join drops the tags of the other levels.
        collection = (request.orderingIsIdentifier_ ?
                      MongoDBMainDicomTags::DICOM_IDENTIFIERS :
                      MongoDBMainDicomTags::MAIN_DICOM_TAGS);
        if (scanGroup == NULL)
        {
          stages.append(make_document(kvp("$match", make_document(
            kvp("tagGroup", request.orderingGroup_),
            kvp("tagElement", request.orderingElement_)))));
        }
        else
        {
          stages.append(make_document(kvp("$match", CreateTagFilter(*scanGroup, false))));
        }

        stages.append(make_document(kvp("$sort", make_document(kvp("value", request.orderingDirection_)))));

        if (request.orderingScanLimit_ > 0)
        {
          stages.append(make_document(kvp("$limit", request.orderingScanLimit_)));
        }

        stages.append(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("id", 1)))));
        AppendCandidateJoin(stages, "id", "internalId", level, candidateProjection.view());
      }
    }
    else if (driver == NULL)
    {
      collection = MongoDBResources::COLLECTION;
      stages.append(make_document(kvp("$match", make_document(kvp("resourceType", level)))));
      stages.append(make_document(kvp("$project", candidateProjection.view())));
    }
    else
    {
      collection = GetCollection(*driver);
      stages.append(make_document(kvp("$match", CreateTagFilter(*driver, false))));
      stages.append(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("id", 1)))));

      if (driver->level_ == level)
      {
        AppendCandidateJoin(stages, "id", "internalId", level, candidateProjection.view());
      }
      else
      {
        /**
         * The resources of the query level list the matching resource
         * in their level array, which is indexed with "resourceType".
         * A numeric "foreignField" is not affected by the MongoDB bug
         * of "GetLevelKey()". The level arrays only contain resources
         * of their level, so the tags of another level cannot match.
         **/
        AppendCandidateJoin(stages, "id", GetLevelKey(driver->level_), level, candidateProjection.view());
      }

      // A resource can be reached through several matching descendants
      if (driver->level_ > level ||
          request.distinct_)
      {
        stages.append(make_document(kvp("$group", make_document(
          kvp("_id", "$internalId"),
          kvp(FIELD_RESOURCE, make_document(kvp("$first", "$$ROOT")))))));
        stages.append(make_document(kvp("$replaceWith", "$" + FIELD_RESOURCE)));
      }
    }

    // 5. The other groups

    for (size_t i = 0; i < sameOrAncestorGroups.size(); i++)
    {
      const TagGroup& group = *sameOrAncestorGroups[i];
      AppendTagGroupFilter(stages, group, group.level_ == level ? "internalId" : GetLevelKey(group.level_));
    }

    if (!descendantLevels.empty())
    {
      AppendDescendantFilter(stages, descendantLevels, 0, descendantGroups);
    }

    if (hasLabelsFilter &&
        !labelsDrive)
    {
      AppendLabelsFilter(stages, labels, labelsConstraint);
    }

    return true;
  }


  void MongoDBLookup::LookupResources(IDatabaseBackendOutput& output,
                                      const DatabaseConstraints& lookup,
                                      OrthancPluginResourceType queryLevel,
                                      const std::set<std::string>& labels,
                                      LabelsConstraint labelsConstraint,
                                      uint32_t limit,
                                      bool requestSomeInstance)
  {
    const bool sort = (queryLevel == OrthancPluginResourceType_Study ||
                       queryLevel == OrthancPluginResourceType_Series);
    const bool someInstance = (requestSomeInstance &&
                               queryLevel != OrthancPluginResourceType_Instance);

    CandidateRequest request(static_cast<int32_t>(queryLevel), lookup);
    request.labels_ = labels;
    request.labelsConstraint_ = labelsConstraint;

    if (sort)
    {
      // Most recent studies and series first, as in the previous versions of the plugin
      request.fields_.insert("sorts");
    }

    if (someInstance)
    {
      // The level array "3" lists the instances of the resource
      request.fields_.insert("instancePublicId");
      request.fields_.insert(GetLevelKey(OrthancPluginResourceType_Instance));
    }

    std::string collection;
    bsoncxx::builder::basic::array stages;

    if (!AppendCandidates(collection, stages, request))
    {
      return;
    }

    // 6. The answers

    if (sort)
    {
      stages.append(make_document(kvp("$sort", make_document(kvp("sorts.0", -1), kvp("sorts.1", -1)))));
    }

    if (limit != 0)
    {
      stages.append(make_document(kvp("$limit", static_cast<int64_t>(limit))));
    }

    if (someInstance)
    {
      stages.append(make_document(kvp("$addFields", make_document(
        kvp(FIELD_FIRST_INSTANCE, make_document(kvp("$first", GetLevelArray(OrthancPluginResourceType_Instance))))))));

      stages.append(make_document(kvp("$lookup", make_document(
        kvp("from", MongoDBResources::COLLECTION),
        kvp("localField", FIELD_FIRST_INSTANCE),
        kvp("foreignField", "internalId"),
        kvp("pipeline", make_array(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("publicId", 1)))))),
        kvp("as", FIELD_INSTANCE)))));

      // "instancePublicId" is written by "CreateInstance()", but can refer to a deleted instance
      stages.append(make_document(kvp("$project", make_document(
        kvp("_id", 0),
        kvp("publicId", 1),
        kvp(FIELD_INSTANCE, make_document(kvp("$ifNull", make_array(
          make_document(kvp("$first", "$" + FIELD_INSTANCE + ".publicId")),
          "$instancePublicId"))))))));
    }
    else
    {
      stages.append(make_document(kvp("$project", make_document(kvp("_id", 0), kvp("publicId", 1)))));
    }

    mongocxx::pipeline pipeline;
    pipeline.append_stages(stages.extract());

    mongocxx::options::aggregate options;
    options.allow_disk_use(true);

    MongoDBCollection::Documents resources;
    database_.GetCollection(collection).Aggregate(resources, pipeline, options);

    for (size_t i = 0; i < resources.size(); i++)
    {
      const bsoncxx::document::view resource = resources[i].view();
      const std::string publicId = MongoDBToolbox::GetString(resource, "publicId");

      if (!requestSomeInstance)
      {
        output.AnswerMatchingResource(publicId);
      }
      else if (queryLevel == OrthancPluginResourceType_Instance)
      {
        output.AnswerMatchingResource(publicId, publicId);
      }
      else
      {
        std::string instance;
        if (MongoDBToolbox::LookupString(instance, resource, FIELD_INSTANCE))
        {
          output.AnswerMatchingResource(publicId, instance);
        }
        else
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_Database, "Resource without instance: " + publicId);
        }
      }
    }
  }
}
