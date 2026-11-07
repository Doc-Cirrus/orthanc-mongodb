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
#include "../../../Framework/Plugins/MessagesToolbox.h"

#include <string>


namespace OrthancDatabases
{
  /**
   * "ExecuteFind()" and "ExecuteCount()", the integrated Find of
   * Orthanc ("supports_find"). Counterpart of "ExecuteFind()" in the
   * SQL "IndexBackend", with the same selection, ordering and limits.
   *
   * Each request is one aggregation pipeline (MongoDB 7.0 or later):
   *
   * 1. The candidates of "MongoDBLookup::AppendCandidates()": the tag
   *    and metadata constraints, the labels, and the resource
   *    identified by its Orthanc ID (always the query level or an
   *    ancestor, as enforced by "FindRequest" in the core).
   *
   * 2. The ordering: one "$lookup" per key, then "$sort", with the
   *    missing values last, and the public ID to break the ties (it
   *    is the only key if the request has no ordering, which makes
   *    "since" repeatable, as in SQL). Then "$skip" and "$limit".
   *
   * 3. The content, only for the resources of the page: one "$lookup"
   *    per retrieval flag. The ancestors, children, grandchildren and
   *    one instance are reached through the level arrays "0" to "3",
   *    which also give the numbers of children.
   **/
  class MongoDBFind : public boost::noncopyable
  {
  private:
    DatabaseManager&  manager_;
    MongoDBDatabase&  database_;

  public:
    explicit MongoDBFind(DatabaseManager& manager);

#if ORTHANC_PLUGINS_HAS_INTEGRATED_FIND == 1
    void ExecuteFind(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                     const Orthanc::DatabasePluginMessages::Find_Request& request);

    void ExecuteCount(Orthanc::DatabasePluginMessages::TransactionResponse& response,
                      const Orthanc::DatabasePluginMessages::Find_Request& request);
#endif
  };
}
