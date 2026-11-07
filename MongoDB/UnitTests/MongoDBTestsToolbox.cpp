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


#include "MongoDBTestsToolbox.h"

#include "../../Framework/MongoDB/MongoDBIncludes.h"

#include <Logging.h>
#include <OrthancException.h>
#include <Toolbox.h>

#include <boost/algorithm/string/replace.hpp>


namespace OrthancDatabases
{
  static std::string connectionUri_;


  static const char* const RESOURCES_SCHEMA = R"({
    "$jsonSchema": {
      "bsonType": "object",
      "required": ["_id", "internalId", "resourceType", "publicId"],
      "properties": {
        "_id": { "bsonType": "objectId" },
        "internalId": { "bsonType": "long" },
        "resourceType": { "bsonType": ["int", "double", "long"] },
        "publicId": { "bsonType": "string" },
        "parentId": { "bsonType": "long" },
        "sorts": { "bsonType": "array", "items": { "bsonType": "string" } },
        "instancePublicId": { "bsonType": "string" }
      },
      "patternProperties": {
        "^[0-9]+$": { "bsonType": "array", "items": { "bsonType": "long" } }
      }
    }
  })";


  void SetTestConnectionUri(const std::string& uri)
  {
    connectionUri_ = uri;
  }


  const std::string& GetTestConnectionUri()
  {
    return connectionUri_;
  }


  std::string SetDatabaseInUri(const std::string& uri,
                               const std::string& database)
  {
    // "mongodb://[user:pass@]host1[:port1][,host2...]/[database][?options]"
    size_t scheme = uri.find("://");
    if (scheme == std::string::npos)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange, "Not a MongoDB URI: " + uri);
    }

    const size_t hostsStart = scheme + 3;
    const size_t query = uri.find('?', hostsStart);
    const size_t slash = uri.find('/', hostsStart);

    std::string hosts, options;

    if (slash != std::string::npos &&
        (query == std::string::npos || slash < query))
    {
      hosts = uri.substr(0, slash);
    }
    else
    {
      hosts = uri.substr(0, query);
    }

    if (query != std::string::npos)
    {
      options = uri.substr(query);
    }

    return hosts + "/" + database + options;
  }


  MongoDBParameters CreateTestParameters(const std::string& uri)
  {
    MongoDBParameters parameters;
    parameters.SetConnectionUri(uri);
    parameters.SetMaxConnectionRetries(0);  // Fail fast if the test server is down
    return parameters;
  }


  bool IsTestServerReplicaSet()
  {
    mongocxx::client client{mongocxx::uri{connectionUri_}};
    auto hello = client["admin"].run_command(bsoncxx::builder::basic::make_document(
      bsoncxx::builder::basic::kvp("hello", 1)));

    return (hello.view().find("setName") != hello.view().end() ||
            (hello.view().find("msg") != hello.view().end() &&
             hello.view()["msg"].get_string().value == "isdbgrid"));
  }


  TestDatabase::TestDatabase()
  {
    std::string uuid = Orthanc::Toolbox::GenerateUuid();
    boost::replace_all(uuid, "-", "");

    name_ = "test_" + uuid;
    uri_ = SetDatabaseInUri(connectionUri_, name_);

    // Every write to "Resources" must respect the document shape
    // described in "MongoDBResources.h"
    mongocxx::client client{mongocxx::uri{uri_}};
    client[name_].create_collection(
      "Resources", bsoncxx::builder::basic::make_document(
        bsoncxx::builder::basic::kvp("validator", bsoncxx::from_json(RESOURCES_SCHEMA)),
        bsoncxx::builder::basic::kvp("validationLevel", "strict"),
        bsoncxx::builder::basic::kvp("validationAction", "error")));
  }


  TestDatabase::~TestDatabase()
  {
    try
    {
      mongocxx::client client{mongocxx::uri{uri_}};
      client[name_].drop();
    }
    catch (std::exception& e)
    {
      LOG(ERROR) << "Cannot drop the test database " << name_ << ": " << e.what();
    }
  }
}
