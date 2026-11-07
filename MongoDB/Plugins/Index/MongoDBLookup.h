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
#include "../../../Framework/Plugins/DatabaseConstraint.h"
#include "../../../Framework/Plugins/IDatabaseBackendOutput.h"

#include <set>
#include <string>
#include <utility>
#include <vector>


namespace OrthancDatabases
{
  /**
   * "LookupResources()", the primitive behind the generic Find of
   * Orthanc while "supports_find" is off, and "AppendCandidates()", the
   * start of the pipelines of "MongoDBFind".
   *
   * The whole lookup is one aggregation pipeline, which needs MongoDB
   * 7.0 or later. The constraints are grouped by tag and level (all
   * the constraints of a group apply to the same tag document), and
   * the semantics follow "ISqlLookupFormatter" of the SQL plugins:
   *
   * - The groups are combined with AND.
   *
   * - The groups on the query level or on an ancestor level apply to
   *   the only resource of that level. The groups on a lower level
   *   must all match the same descendant (and the descendants of
   *   several lower levels form one chain), like the "INNER JOIN" of
   *   one row per level in SQL.
   *
   * - A group that is not mandatory also matches if the tag is
   *   missing.
   *
   * The pipeline starts from the tag collection of the most selective
   * mandatory group (the "driver"), which yields the candidate
   * resources through the indexes on their level arrays "0" to "3"
   * (one level array lists at most the resources of one patient, i.e.
   * a few thousand IDs, which is small for an index). The selectivity
   * is measured by counting the matching tags, up to a small limit.
   * Each remaining group is then a correlated "$lookup" on the tag
   * collections.
   **/
  class MongoDBLookup : public boost::noncopyable
  {
  public:
    /**
     * The resources to select. Besides the tag constraints and the
     * labels, "ExecuteFind()" adds constraints on the metadata of the
     * query level (same semantics as the tags), and one resource
     * identified by its Orthanc ID, which must be an ancestor of the
     * candidates, a descendant of them, or one of them.
     **/
    struct CandidateRequest
    {
      int32_t                                                     level_;
      const DatabaseConstraints*                                  tags_;
      std::vector<std::pair<int32_t, const DatabaseConstraint*> > metadata_;  // Metadata type, and constraint
      std::set<std::string>                                       labels_;
      LabelsConstraint                                            labelsConstraint_;
      bool                                                        hasIdentified_;
      int32_t                                                     identifiedLevel_;
      int64_t                                                     identifiedId_;
      std::set<std::string>                                       fields_;  // Other fields to keep, e.g. level arrays

      /**
       * Whether each candidate must come once even if it has several
       * documents for the tag of the driver (which Orthanc never
       * writes). This costs a "$group", which the "$sort" of a Find
       * makes cheap.
       **/
      bool                                                        distinct_;

      /**
       * If nothing selective can start the pipeline (no identified
       * resource, no label to have, and no mandatory tag matched by
       * fewer than "PROBE_LIMIT" tags), the candidates can come from
       * an index in the order of a key: the values of a tag of the
       * query level ("orderingDirection_" is 1 or -1), or the public
       * IDs if "orderingOnPublicId_". The resources without this tag
       * are left out. The mandatory tags are then filters. If
       * "orderingScanLimit_" is not 0, only this number of first
       * entries of the index are read, which bounds the cost if the
       * filters reject more resources than expected.
       **/
      bool                                                        hasOrderingScan_;
      bool                                                        orderingOnPublicId_;
      bool                                                        orderingIsIdentifier_;
      uint16_t                                                    orderingGroup_;
      uint16_t                                                    orderingElement_;
      int32_t                                                     orderingDirection_;
      int64_t                                                     orderingScanLimit_;

      // If not NULL, the group of this constraint (of "tags_") is preferred to start the pipeline
      const DatabaseConstraint*                                   preferredDriver_;

      CandidateRequest(int32_t level,
                       const DatabaseConstraints& tags) :
        level_(level),
        tags_(&tags),
        labelsConstraint_(LabelsConstraint_All),
        hasIdentified_(false),
        identifiedLevel_(0),
        identifiedId_(0),
        distinct_(false),
        hasOrderingScan_(false),
        orderingOnPublicId_(false),
        orderingIsIdentifier_(false),
        orderingGroup_(0),
        orderingElement_(0),
        orderingDirection_(1),
        orderingScanLimit_(0),
        preferredDriver_(NULL)
      {
      }
    };

  private:
    MongoDBDatabase&  database_;

  public:
    explicit MongoDBLookup(DatabaseManager& manager);

    /**
     * Returns "false" if no resource can match. Otherwise, aggregating
     * "stages" on "collection" gives one document per candidate, with
     * its "internalId", its "publicId" and the "fields_" of the request.
     * Appending a "$lookup" whose "localField" is a level array needs
     * this array in "fields_" (cf. "GetLevelKey()").
     **/
    bool AppendCandidates(std::string& collection,
                          bsoncxx::builder::basic::array& stages,
                          const CandidateRequest& request);

    /**
     * Same as above. "orderingScan" tells whether the candidates come
     * from the ordering scan of "hasOrderingScan_", in its order.
     **/
    bool AppendCandidates(std::string& collection,
                          bsoncxx::builder::basic::array& stages,
                          bool& orderingScan,
                          const CandidateRequest& request);

    void LookupResources(IDatabaseBackendOutput& output,
                         const DatabaseConstraints& lookup,
                         OrthancPluginResourceType queryLevel,
                         const std::set<std::string>& labels,
                         LabelsConstraint labelsConstraint,
                         uint32_t limit,
                         bool requestSomeInstance);

    /**
     * The filters on a tag document that implement the constraint,
     * appended to "target" (they are combined with AND). Returns
     * "false" if the constraint matches any value (the wildcard "*").
     **/
    static bool AppendValueConditions(std::vector<bsoncxx::document::value>& target,
                                      const DatabaseConstraint& constraint);

    /**
     * Static estimate of the cost of starting the pipeline from this
     * constraint: the lower, the fewer index keys are likely to be
     * scanned. Only breaks the ties between the counts of matching tags.
     **/
    static unsigned int GetSelectivityCost(const DatabaseConstraint& constraint);

    /**
     * Whether the condition of the constraint on "value" bounds the
     * scan of the index on (tagGroup, tagElement, value), so that
     * counting its matching tags up to a limit reads few index keys.
     **/
    static bool IsIndexBounded(const DatabaseConstraint& constraint);
  };
}
