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


#include "MongoDBStatistics.h"

#include "../../../Framework/Plugins/GlobalProperties.h"

#include <Logging.h>
#include <OrthancException.h>
#include <Toolbox.h>

#include <boost/thread/thread.hpp>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  static const char* const CHANGES = "StatisticsChanges";
  static const char* const TOTALS = "GlobalProperties";
  static const int32_t TOTALS_PROPERTY = Orthanc::GlobalProperty_DatabaseInternal0;

  static const std::string COUNT_FIELDS[4] = { "patients", "studies", "series", "instances" };
  static const std::string COMPRESSED_SIZE = "compressedSize";
  static const std::string UNCOMPRESSED_SIZE = "uncompressedSize";
  static const std::string APPLIED_CLAIMS = "appliedClaims";


  static int64_t ReadNumber(const bsoncxx::document::view& document,
                            const std::string& field)
  {
    bsoncxx::document::element element = document[field];

    if (!element)
    {
      return 0;
    }

    switch (element.type())
    {
      case bsoncxx::type::k_int32:
        return element.get_int32().value;

      case bsoncxx::type::k_int64:
        return element.get_int64().value;

      case bsoncxx::type::k_double:
        return static_cast<int64_t>(element.get_double().value);

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database,
                                        "The statistics field " + field + " is not a number");
    }
  }


  MongoDBStatisticsValues::MongoDBStatisticsValues() :
    compressedSize_(0),
    uncompressedSize_(0)
  {
    for (size_t i = 0; i < 4; i++)
    {
      counts_[i] = 0;
    }
  }


  bool MongoDBStatisticsValues::IsZero() const
  {
    return (counts_[0] == 0 && counts_[1] == 0 && counts_[2] == 0 && counts_[3] == 0 &&
            compressedSize_ == 0 && uncompressedSize_ == 0);
  }


  MongoDBStatisticsValues& MongoDBStatisticsValues::operator+= (const MongoDBStatisticsValues& other)
  {
    for (size_t i = 0; i < 4; i++)
    {
      counts_[i] += other.counts_[i];
    }

    compressedSize_ += other.compressedSize_;
    uncompressedSize_ += other.uncompressedSize_;
    return *this;
  }


  MongoDBStatisticsValues& MongoDBStatisticsValues::operator-= (const MongoDBStatisticsValues& other)
  {
    for (size_t i = 0; i < 4; i++)
    {
      counts_[i] -= other.counts_[i];
    }

    compressedSize_ -= other.compressedSize_;
    uncompressedSize_ -= other.uncompressedSize_;
    return *this;
  }


  bsoncxx::document::value MongoDBStatisticsValues::ToDocument() const
  {
    bsoncxx::builder::basic::document document;

    for (size_t i = 0; i < 4; i++)
    {
      if (counts_[i] != 0)
      {
        document.append(kvp(COUNT_FIELDS[i], counts_[i]));
      }
    }

    if (compressedSize_ != 0)
    {
      document.append(kvp(COMPRESSED_SIZE, compressedSize_));
    }

    if (uncompressedSize_ != 0)
    {
      document.append(kvp(UNCOMPRESSED_SIZE, uncompressedSize_));
    }

    return document.extract();
  }


  void MongoDBStatisticsValues::FromDocument(const bsoncxx::document::view& document)
  {
    for (size_t i = 0; i < 4; i++)
    {
      counts_[i] = ReadNumber(document, COUNT_FIELDS[i]);
    }

    compressedSize_ = ReadNumber(document, COMPRESSED_SIZE);
    uncompressedSize_ = ReadNumber(document, UNCOMPRESSED_SIZE);
  }


  static bsoncxx::document::value GetTotalsFilter()
  {
    return make_document(kvp("property", TOTALS_PROPERTY));
  }


  /**
   * The claims whose changes are already added to the totals, but not
   * deleted yet (cf. "Consolidate()"). Their changes are not pending.
   **/
  static bsoncxx::array::value GetAppliedClaims(const bsoncxx::document::view& totals)
  {
    bsoncxx::builder::basic::array claims;
    bsoncxx::document::element applied = totals[APPLIED_CLAIMS];

    if (applied &&
        applied.type() == bsoncxx::type::k_array)
    {
      for (const bsoncxx::array::element& claim : applied.get_array().value)
      {
        claims.append(claim.get_value());
      }
    }

    return claims.extract();
  }


  static bsoncxx::document::value GetPendingFilter(const bsoncxx::document::view& totals)
  {
    return make_document(kvp("claim", make_document(kvp("$nin", GetAppliedClaims(totals)))));
  }


  // Sums the documents of "collection" that match "filter"
  static void SumDocuments(MongoDBStatisticsValues& target,
                           mongocxx::collection collection,
                           const mongocxx::client_session* session,
                           const bsoncxx::document::view_or_value& filter)
  {
    bsoncxx::builder::basic::document group;
    group.append(kvp("_id", bsoncxx::types::b_null()));

    for (size_t i = 0; i < 4; i++)
    {
      group.append(kvp(COUNT_FIELDS[i], make_document(kvp("$sum", std::string("$") + COUNT_FIELDS[i]))));
    }

    group.append(kvp(COMPRESSED_SIZE, make_document(kvp("$sum", std::string("$") + COMPRESSED_SIZE))),
                 kvp(UNCOMPRESSED_SIZE, make_document(kvp("$sum", std::string("$") + UNCOMPRESSED_SIZE))));

    mongocxx::pipeline pipeline;
    pipeline.match(filter);
    pipeline.group(group.extract());

    target = MongoDBStatisticsValues();

    mongocxx::cursor cursor = (session == NULL ?
                               collection.aggregate(pipeline) :
                               collection.aggregate(*session, pipeline));

    for (const bsoncxx::document::view& result : cursor)
    {
      target.FromDocument(result);
    }
  }


  static int64_t CountDocuments(mongocxx::collection collection,
                                const mongocxx::client_session* session,
                                const bsoncxx::document::view_or_value& filter)
  {
    return (session == NULL ?
            collection.count_documents(filter) :
            collection.count_documents(*session, filter));
  }


  void MongoDBStatistics::Scan(MongoDBStatisticsValues& target,
                               mongocxx::database& database,
                               const mongocxx::client_session* session)
  {
    // The attachments do not have the fields of the resource counts, which sum to 0
    SumDocuments(target, database["AttachedFiles"], session, make_document());

    for (int32_t level = 0; level < 4; level++)
    {
      target.counts_[level] = CountDocuments(database["Resources"], session, make_document(kvp("resourceType", level)));
    }
  }


  MongoDBStatistics::MongoDBStatistics(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  MongoDBStatistics::MongoDBStatistics(MongoDBDatabase& database) :
    database_(database)
  {
  }


  std::optional<bsoncxx::document::value> MongoDBStatistics::LookupTotals()
  {
    return database_.GetCollection(TOTALS).FindOne(GetTotalsFilter());
  }


  void MongoDBStatistics::SumChanges(MongoDBStatisticsValues& target,
                                     const bsoncxx::document::view_or_value& filter)
  {
    try
    {
      SumDocuments(target, database_.GetDatabase()[CHANGES], database_.GetSession(), filter);
    }
    catch (mongocxx::exception& e)
    {
      MongoDBDatabase::ThrowException(e);
    }
  }


  void MongoDBStatistics::RecordChange(const MongoDBStatisticsValues& change)
  {
    if (!change.IsZero())
    {
      database_.GetCollection(CHANGES).InsertOne(change.ToDocument());
    }
  }


  void MongoDBStatistics::GetValues(MongoDBStatisticsValues& target)
  {
    std::optional<bsoncxx::document::value> totals = LookupTotals();

    if (totals)
    {
      target.FromDocument(totals->view());

      MongoDBStatisticsValues pending;
      SumChanges(pending, GetPendingFilter(totals->view()));
      target += pending;
    }
    else
    {
      try
      {
        mongocxx::database database = database_.GetDatabase();
        Scan(target, database, database_.GetSession());
      }
      catch (mongocxx::exception& e)
      {
        MongoDBDatabase::ThrowException(e);
      }
    }
  }


  bool MongoDBStatistics::Consolidate()
  {
    std::optional<bsoncxx::document::value> totals = LookupTotals();

    if (!totals)
    {
      return false;
    }

    MongoDBCollection changes = database_.GetCollection(CHANGES);
    MongoDBCollection properties = database_.GetCollection(TOTALS);

    /**
     * Without transactions, the "$inc" of the totals and the deletion of
     * the changes are separate writes. The totals therefore record the
     * claim they have added: a consolidation interrupted in between is
     * finished here, and its changes are never added twice.
     **/
    const bsoncxx::array::value applied = GetAppliedClaims(totals->view());
    for (const bsoncxx::array::element& claim : applied.view())
    {
      changes.DeleteMany(make_document(kvp("claim", claim.get_value())));
      properties.UpdateOne(GetTotalsFilter(),
                           make_document(kvp("$pull", make_document(kvp(APPLIED_CLAIMS, claim.get_value())))));
    }

    /**
     * Each change is claimed by a single consolidation (one update of
     * a document is atomic), so that two concurrent consolidations
     * never add the same change twice.
     **/
    const std::string claim = Orthanc::Toolbox::GenerateUuid();

    if (changes.UpdateMany(make_document(kvp("claim", make_document(kvp("$exists", false)))),
                           make_document(kvp("$set", make_document(kvp("claim", claim))))) > 0)
    {
      MongoDBStatisticsValues sum;
      SumChanges(sum, make_document(kvp("claim", claim)));

      bsoncxx::builder::basic::document update;
      update.append(kvp("$addToSet", make_document(kvp(APPLIED_CLAIMS, claim))));

      if (!sum.IsZero())
      {
        update.append(kvp("$inc", sum.ToDocument()));
      }

      properties.UpdateOne(make_document(kvp("property", TOTALS_PROPERTY),
                                         kvp(APPLIED_CLAIMS, make_document(kvp("$ne", claim)))),
                           update.extract());

      changes.DeleteMany(make_document(kvp("claim", claim)));
      properties.UpdateOne(GetTotalsFilter(), make_document(kvp("$pull", make_document(kvp(APPLIED_CLAIMS, claim)))));
    }

    return true;
  }


  static bool StoreTotals(mongocxx::database& database,
                          const MongoDBStatisticsValues& totals)
  {
    bsoncxx::builder::basic::document document;
    document.append(kvp("property", TOTALS_PROPERTY),
                    kvp("value", ""));  // The previous versions read "value" as a string

    for (size_t i = 0; i < 4; i++)
    {
      document.append(kvp(COUNT_FIELDS[i], totals.counts_[i]));
    }

    document.append(kvp(COMPRESSED_SIZE, totals.compressedSize_),
                    kvp(UNCOMPRESSED_SIZE, totals.uncompressedSize_));

    try
    {
      database[TOTALS].insert_one(document.view());
      return true;
    }
    catch (mongocxx::operation_exception& e)
    {
      if (e.code().value() == 11000 /* DuplicateKey */)
      {
        return false;  // Another Orthanc has stored them meanwhile
      }
      else
      {
        throw;
      }
    }
  }


  bool MongoDBStatistics::ComputeTotals(bool force)
  {
    if (LookupTotals())
    {
      return true;
    }

    /**
     * The totals are the values at some point in time, minus the
     * changes pending at the same point in time: the changes recorded
     * afterwards are added by "GetValues()" and "Consolidate()". The
     * consolidations do nothing until the totals exist, so the pending
     * changes are all the changes recorded so far.
     **/
    MongoDBStatisticsValues totals;
    MongoDBStatisticsValues pending;

    try
    {
      mongocxx::database database = database_.GetDatabase();

      if (database_.HasTransactions())
      {
        mongocxx::options::client_session options;
        options.snapshot(true);
        mongocxx::client_session session = database_.GetClient().start_session(options);

        Scan(totals, database, &session);
        SumDocuments(pending, database[CHANGES], &session, make_document());
      }
      else
      {
        /**
         * Every write changes the data first, and records its change
         * right after. A write whose data the scan has seen may record
         * its change after the scan: the changes are therefore counted
         * again once such writes have had time to finish. Only a writer
         * stalled longer than "SETTLE" between its two writes escapes.
         **/
        static const unsigned int SETTLE = 1000;  // Milliseconds

        const int64_t before = CountDocuments(database[CHANGES], NULL, make_document());
        Scan(totals, database, NULL);
        SumDocuments(pending, database[CHANGES], NULL, make_document());
        boost::this_thread::sleep(boost::posix_time::milliseconds(SETTLE));
        const int64_t after = CountDocuments(database[CHANGES], NULL, make_document());

        if (before != after)
        {
          if (force)
          {
            LOG(WARNING) << "MongoDB: the statistics are computed while " << (after - before)
                         << " changes were written: these may be miscounted until the totals are "
                         << "computed again (delete the property " << TOTALS_PROPERTY << " of GlobalProperties)";
          }
          else
          {
            return false;
          }
        }
      }

      totals -= pending;

      if (StoreTotals(database, totals))
      {
        totals += pending;
        LOG(WARNING) << "MongoDB: the statistics are computed: " << totals.counts_[OrthancPluginResourceType_Instance]
                     << " instances, " << totals.compressedSize_ << " bytes";
      }
    }
    catch (mongocxx::exception& e)
    {
      MongoDBDatabase::ThrowException(e);
    }

    return true;
  }


  void MongoDBStatistics::InitializeIfEmpty()
  {
    try
    {
      mongocxx::database database = database_.GetDatabase();

      if (!database[TOTALS].find_one(GetTotalsFilter()) &&
          !database["Resources"].find_one(make_document()) &&
          !database["AttachedFiles"].find_one(make_document()))
      {
        MongoDBStatisticsValues pending;
        SumDocuments(pending, database[CHANGES], NULL, make_document());

        MongoDBStatisticsValues totals;
        totals -= pending;
        StoreTotals(database, totals);
      }
    }
    catch (mongocxx::exception& e)
    {
      MongoDBDatabase::ThrowException(e);
    }
  }
}
