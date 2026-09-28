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


#include "MongoDBGlobalProperties.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>

#include <cstring>
#include <sstream>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_array;
  using bsoncxx::builder::basic::make_document;


  static bsoncxx::document::value CreateFilter(const char* serverIdentifier,
                                               int32_t property)
  {
    if (serverIdentifier == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }
    else if (strlen(serverIdentifier) == 0)
    {
      return make_document(kvp("property", property));
    }
    else
    {
      return make_document(kvp("property", property), kvp("server", serverIdentifier));
    }
  }


  static const char* GetCollectionName(const char* serverIdentifier)
  {
    return (strlen(serverIdentifier) == 0 ? "GlobalProperties" : "ServerProperties");
  }


  // Leaves room for the other fields in the 16MB of a BSON document
  static const size_t MAX_INLINE_SIZE = 15 * 1024 * 1024;

  static const std::string FILE_FIELD = "valueFile";


  mongocxx::gridfs::bucket MongoDBGlobalProperties::GetBucket()
  {
    mongocxx::options::gridfs::bucket options;
    options.bucket_name("LargeProperties");
    return database_.GetDatabase().gridfs_bucket(options);
  }


  std::string MongoDBGlobalProperties::ReadFile(const bsoncxx::types::bson_value::view& file)
  {
    std::ostringstream stream;

    try
    {
      mongocxx::gridfs::bucket bucket = GetBucket();

      if (database_.GetSession() == NULL)
      {
        bucket.download_to_stream(file, &stream);
      }
      else
      {
        bucket.download_to_stream(*database_.GetSession(), file, &stream);
      }
    }
    catch (mongocxx::gridfs_exception& e)
    {
      if (e.code() == mongocxx::error_code::k_gridfs_file_not_found)
      {
        // Without a transaction, a concurrent writer has replaced the
        // value since its document was read: a retry reads the new one
        throw Orthanc::OrthancException(Orthanc::ErrorCode_DatabaseCannotSerialize, e.what(), false);
      }
      else
      {
        MongoDBDatabase::ThrowException(e);
      }
    }
    catch (mongocxx::exception& e)
    {
      MongoDBDatabase::ThrowException(e);
    }

    return stream.str();
  }


  void MongoDBGlobalProperties::DeleteFile(const bsoncxx::types::bson_value::view& file)
  {
    try
    {
      mongocxx::gridfs::bucket bucket = GetBucket();

      if (database_.GetSession() == NULL)
      {
        bucket.delete_file(file);
      }
      else
      {
        bucket.delete_file(*database_.GetSession(), file);
      }
    }
    catch (mongocxx::gridfs_exception& e)
    {
      if (e.code() != mongocxx::error_code::k_gridfs_file_not_found)
      {
        MongoDBDatabase::ThrowException(e);
      }
    }
    catch (mongocxx::exception& e)
    {
      MongoDBDatabase::ThrowException(e);
    }
  }


  MongoDBGlobalProperties::MongoDBGlobalProperties(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  bool MongoDBGlobalProperties::LookupGlobalProperty(std::string& target /*out*/,
                                                     const char* serverIdentifier,
                                                     int32_t property)
  {
    bsoncxx::document::value filter = CreateFilter(serverIdentifier, property);

    std::optional<bsoncxx::document::value> document =
      database_.GetCollection(GetCollectionName(serverIdentifier)).FindOne(filter.view());

    if (document)
    {
      bsoncxx::document::element file = document->view()[FILE_FIELD];

      if (file)
      {
        target = ReadFile(file.get_value());
      }
      else
      {
        target = MongoDBToolbox::GetString(document->view(), "value");
      }

      return true;
    }
    else
    {
      return false;
    }
  }


  void MongoDBGlobalProperties::SetGlobalProperty(const char* serverIdentifier,
                                                  int32_t property,
                                                  const char* utf8)
  {
    if (utf8 == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    bsoncxx::document::value filter = CreateFilter(serverIdentifier, property);

    const size_t size = strlen(utf8);

    /**
     * The new file is uploaded first, then the document switches to it
     * in a single atomic operation, which returns the previous document:
     * each replaced file is deleted by exactly one writer. Without a
     * transaction, an interruption can only leave an unused file.
     **/
    std::optional<bsoncxx::types::bson_value::value> file;
    bsoncxx::document::value update = make_document(
      kvp("$set", make_document(kvp("value", utf8))),
      kvp("$unset", make_document(kvp(FILE_FIELD, ""))));

    if (size > MAX_INLINE_SIZE)
    {
      const std::string filename = std::string(GetCollectionName(serverIdentifier)) + " " +
        (strlen(serverIdentifier) == 0 ? "" : std::string(serverIdentifier) + " ") + std::to_string(property);

      std::istringstream stream(std::string(utf8, size));

      try
      {
        mongocxx::gridfs::bucket bucket = GetBucket();

        mongocxx::result::gridfs::upload result = (database_.GetSession() == NULL ?
                                                   bucket.upload_from_stream(filename, &stream) :
                                                   bucket.upload_from_stream(*database_.GetSession(), filename, &stream));
        file = bsoncxx::types::bson_value::value(result.id());
      }
      catch (mongocxx::exception& e)
      {
        MongoDBDatabase::ThrowException(e);
      }

      update = make_document(kvp("$set", make_document(kvp("value", ""), kvp(FILE_FIELD, file->view()))));
    }

    mongocxx::options::find_one_and_update options;
    options.upsert(true);
    options.return_document(mongocxx::options::return_document::k_before);

    std::optional<bsoncxx::document::value> previous =
      database_.GetCollection(GetCollectionName(serverIdentifier)).FindOneAndUpdate(filter.view(), update.view(), options);

    if (previous &&
        previous->view()[FILE_FIELD])
    {
      DeleteFile(previous->view()[FILE_FIELD].get_value());
    }
  }


  int64_t MongoDBGlobalProperties::IncrementGlobalProperty(const char* serverIdentifier,
                                                           int32_t property,
                                                           int64_t increment)
  {
    bsoncxx::document::value filter = CreateFilter(serverIdentifier, property);

    // value = String(Long(value or "0") + increment), computed by the server
    mongocxx::pipeline update;
    update.add_fields(make_document(
      kvp("value", make_document(
            kvp("$toString", make_document(
                  kvp("$add", make_array(
                        make_document(kvp("$toLong", make_document(kvp("$ifNull", make_array("$value", "0"))))),
                        increment))))))));

    mongocxx::options::find_one_and_update options;
    options.upsert(true);
    options.return_document(mongocxx::options::return_document::k_after);

    std::optional<bsoncxx::document::value> document =
      database_.GetCollection(GetCollectionName(serverIdentifier)).FindOneAndUpdate(filter.view(), update, options);

    if (!document)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
    }

    const std::string value = MongoDBToolbox::GetString(document->view(), "value");

    try
    {
      return boost::lexical_cast<int64_t>(value);
    }
    catch (boost::bad_lexical_cast&)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_Database,
                                      "Global property " + std::to_string(property) + " is not an integer: " + value);
    }
  }
}
