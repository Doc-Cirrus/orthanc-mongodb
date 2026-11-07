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


#include "MongoDBAuditLogs.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"

#include <OrthancException.h>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <chrono>
#include <cstdio>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_array;
  using bsoncxx::builder::basic::make_document;


  const char* const MongoDBAuditLogs::COLLECTION = "AuditLogs";


  // "2026-09-24T10:00:00.123Z", as "to_char(ts, 'YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"')" in PostgreSQL
  static std::string FormatTimestamp(const bsoncxx::types::b_date& date)
  {
    const int64_t milliseconds = date.to_int64();
    const int64_t seconds = (milliseconds >= 0 ? milliseconds / 1000 : (milliseconds - 999) / 1000);

    const boost::posix_time::ptime time =
      boost::posix_time::from_time_t(static_cast<time_t>(seconds));

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s.%03dZ",
             boost::posix_time::to_iso_extended_string(time).c_str(),
             static_cast<int>(milliseconds - seconds * 1000));
    return buffer;
  }


  /**
   * Converts the "from" and "to" filters into dates. The server parses
   * them with "$dateFromString", which accepts the same ISO 8601 forms
   * as a "::TIMESTAMPTZ" cast (dates, times, offsets and "Z"), so that
   * the query itself compares plain dates and uses the index.
   **/
  static void ParseTimestamps(std::optional<bsoncxx::types::b_date>& from,
                              std::optional<bsoncxx::types::b_date>& to,
                              mongocxx::database database,
                              const std::string& fromTsIsoFormat,
                              const std::string& toTsIsoFormat)
  {
    bsoncxx::builder::basic::document dates;

    if (!fromTsIsoFormat.empty())
    {
      dates.append(kvp("from", make_document(kvp("$dateFromString", make_document(kvp("dateString", fromTsIsoFormat))))));
    }

    if (!toTsIsoFormat.empty())
    {
      dates.append(kvp("to", make_document(kvp("$dateFromString", make_document(kvp("dateString", toTsIsoFormat))))));
    }

    mongocxx::pipeline pipeline;
    pipeline.append_stage(make_document(kvp("$documents", make_array(make_document()))));
    pipeline.project(make_document(kvp("_id", 0), bsoncxx::builder::concatenate(dates.extract())));

    try
    {
      for (const bsoncxx::document::view& result : database.aggregate(pipeline))
      {
        if (result["from"] && result["from"].type() == bsoncxx::type::k_date)
        {
          from = result["from"].get_date();
        }

        if (result["to"] && result["to"].type() == bsoncxx::type::k_date)
        {
          to = result["to"].get_date();
        }
      }
    }
    catch (mongocxx::operation_exception& e)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                      "Invalid date in the audit logs query: " + std::string(e.what()));
    }
  }


  MongoDBAuditLogs::MongoDBAuditLogs(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBAuditLogs::RecordAuditLog(const std::string& sourcePlugin,
                                        const std::string& userId,
                                        OrthancPluginResourceType resourceType,
                                        const std::string& resourceId,
                                        const std::string& action,
                                        const void* logData,
                                        uint32_t logDataSize)
  {
    // The clock of Orthanc, in milliseconds (PostgreSQL uses the clock of its server)
    bsoncxx::builder::basic::document log;
    log.append(kvp("ts", bsoncxx::types::b_date(std::chrono::system_clock::now())),
               kvp("sourcePlugin", sourcePlugin),
               kvp("userId", userId),
               kvp("resourceType", static_cast<int32_t>(resourceType)),
               kvp("resourceId", resourceId),
               kvp("action", action));

    std::string data;
    if (logData != NULL &&
        logDataSize > 0)
    {
      data.assign(reinterpret_cast<const char*>(logData), logDataSize);
      log.append(kvp("logData", MongoDBToolbox::ToBinary(data)));
    }

    database_.GetCollection(COLLECTION).InsertOne(log.extract());
  }


  void MongoDBAuditLogs::GetAuditLogs(std::list<IDatabaseBackend::AuditLog>& logs,
                                      const std::string& userIdFilter,
                                      const std::string& resourceIdFilter,
                                      const std::string& actionFilter,
                                      const std::string& fromTsIsoFormat,
                                      const std::string& toTsIsoFormat,
                                      uint64_t since,
                                      uint64_t limit)
  {
    bsoncxx::builder::basic::document filter;

    if (!userIdFilter.empty())
    {
      filter.append(kvp("userId", userIdFilter));
    }

    if (!resourceIdFilter.empty())
    {
      filter.append(kvp("resourceId", resourceIdFilter));
    }

    if (!actionFilter.empty())
    {
      filter.append(kvp("action", actionFilter));
    }

    if (!fromTsIsoFormat.empty() ||
        !toTsIsoFormat.empty())
    {
      std::optional<bsoncxx::types::b_date> from, to;
      ParseTimestamps(from, to, database_.GetDatabase(), fromTsIsoFormat, toTsIsoFormat);

      bsoncxx::builder::basic::document range;
      if (from)
      {
        range.append(kvp("$gte", *from));
      }

      if (to)
      {
        range.append(kvp("$lt", *to));
      }

      filter.append(kvp("ts", range.extract()));
    }

    mongocxx::options::find options;
    options.sort(make_document(kvp("ts", 1), kvp("_id", 1)));

    if (since > 0)
    {
      options.skip(static_cast<int64_t>(since));
    }

    if (limit > 0)
    {
      options.limit(static_cast<int64_t>(limit));
    }

    MongoDBCollection::Documents documents;
    database_.GetCollection(COLLECTION).Find(documents, filter.extract(), options);

    logs.clear();

    for (size_t i = 0; i < documents.size(); i++)
    {
      const bsoncxx::document::view log = documents[i].view();

      bsoncxx::document::element ts = log["ts"];
      if (!ts || ts.type() != bsoncxx::type::k_date)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database, "Audit log without date");
      }

      const bool hasLogData = static_cast<bool>(log["logData"]);

      logs.push_back(IDatabaseBackend::AuditLog(
                       FormatTimestamp(ts.get_date()),
                       MongoDBToolbox::GetString(log, "sourcePlugin"),
                       MongoDBToolbox::GetString(log, "userId"),
                       static_cast<OrthancPluginResourceType>(MongoDBToolbox::GetInt32(log, "resourceType")),
                       MongoDBToolbox::GetString(log, "resourceId"),
                       MongoDBToolbox::GetString(log, "action"),
                       hasLogData ? MongoDBToolbox::GetBinary(log, "logData") : std::string(),
                       hasLogData));
    }
  }
}
