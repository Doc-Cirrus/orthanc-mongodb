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

#include "../../Framework/MongoDB/MongoDBParameters.h"

#include <boost/noncopyable.hpp>
#include <string>


namespace OrthancDatabases
{
  /**
   * Connection URI given on the command line of the unit tests,
   * e.g. "mongodb://localhost:27017/" or
   * "mongodb://localhost:27018/?replicaSet=rs0". The database part of
   * the URI, if any, is ignored: each test uses its own database.
   **/
  void SetTestConnectionUri(const std::string& uri);

  const std::string& GetTestConnectionUri();

  // Replaces the database of a MongoDB connection URI, keeping its options
  std::string SetDatabaseInUri(const std::string& uri,
                               const std::string& database);

  // Default parameters of the plugin, connected to the given URI
  MongoDBParameters CreateTestParameters(const std::string& uri);

  // Whether the test server supports multi-document transactions
  bool IsTestServerReplicaSet();


  /**
   * Random "test_<uuid>" database, dropped when this object is
   * destroyed. Declare it before the objects that connect to it. Its
   * "Resources" collection is created with a strict validator for the
   * document shape described in "MongoDBResources.h".
   **/
  class TestDatabase : public boost::noncopyable
  {
  private:
    std::string  name_;
    std::string  uri_;

  public:
    TestDatabase();

    ~TestDatabase();

    const std::string& GetName() const
    {
      return name_;
    }

    const std::string& GetUri() const
    {
      return uri_;
    }
  };
}
