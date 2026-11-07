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


#include "MongoDBStorageArea.h"

#include "../../Framework/MongoDB/MongoDBDatabase.h"
#include "../../Framework/MongoDB/MongoDBToolbox.h"

#include <OrthancException.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  static const char* const FILES = "fs.files";
  static const char* const CHUNKS = "fs.chunks";


  class MongoDBStorageArea::Accessor : public StorageBackend::IAccessor
  {
  private:
    MongoDBStorageArea&    that_;
    mongocxx::pool::entry  client_;

    mongocxx::database GetDatabase()
    {
      return (*client_)[that_.databaseName_];
    }

    mongocxx::gridfs::bucket GetBucket()
    {
      mongocxx::options::gridfs::bucket options;
      options.chunk_size_bytes(that_.chunkSize_);
      return GetDatabase().gridfs_bucket(options);
    }

    /**
     * The document of "fs.files" of an attachment. If several files
     * have its name (Orthanc retried a "Create()" that had in fact
     * succeeded), the most recent one is taken.
     **/
    bsoncxx::document::value FindFile(const std::string& uuid,
                                      OrthancPluginContentType type)
    {
      mongocxx::options::find options;
      options.projection(make_document(kvp("_id", 1), kvp("length", 1), kvp("chunkSize", 1)));
      options.sort(make_document(kvp("uploadDate", -1), kvp("_id", -1)));  // "_id" if uploaded in the same millisecond

      std::optional<bsoncxx::document::value> file =
        GetDatabase()[FILES].find_one(make_document(kvp("filename", GetFileName(uuid, type))), options);

      if (!file)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource,
                                        "No file in the MongoDB storage area for attachment " + uuid);
      }

      return *file;
    }

    /**
     * Reads "length" bytes of the file from "start", from the chunks
     * that hold them. Each chunk is checked, so that a missing or
     * truncated chunk fails instead of giving wrong content.
     **/
    void Read(std::string& target,
              const bsoncxx::document::view& file,
              uint64_t start,
              uint64_t length)
    {
      const int64_t fileLength = MongoDBToolbox::GetInteger(file, "length");
      const int64_t chunkSize = MongoDBToolbox::GetInteger(file, "chunkSize");

      if (fileLength < 0 ||
          chunkSize <= 0)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database, "Bad GridFS file in the MongoDB storage area");
      }

      if (start > static_cast<uint64_t>(fileLength) ||
          length > static_cast<uint64_t>(fileLength) - start)
      {
        // Same error as "Orthanc::SystemToolbox::ReadFileRange()"
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange,
                                        "Range past the end of a file of the MongoDB storage area");
      }

      target.resize(static_cast<size_t>(length));

      if (length == 0)
      {
        return;
      }

      const int64_t end = static_cast<int64_t>(start + length);  // Exclusive
      const int64_t firstChunk = static_cast<int64_t>(start) / chunkSize;
      const int64_t lastChunk = (end - 1) / chunkSize;
      const int64_t lastChunkOfFile = (fileLength - 1) / chunkSize;

      mongocxx::options::find options;
      options.projection(make_document(kvp("_id", 0), kvp("n", 1), kvp("data", 1)));
      options.sort(make_document(kvp("n", 1)));

      mongocxx::cursor cursor = GetDatabase()[CHUNKS].find(
        make_document(kvp("files_id", file["_id"].get_value()),
                      kvp("n", make_document(kvp("$gte", firstChunk), kvp("$lte", lastChunk)))), options);

      int64_t expected = firstChunk;

      for (const bsoncxx::document::view& chunk : cursor)
      {
        const int64_t n = MongoDBToolbox::GetInteger(chunk, "n");
        const bsoncxx::document::element data = chunk["data"];

        if (n != expected ||
            !data ||
            data.type() != bsoncxx::type::k_binary)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_Database,
                                          "Missing or bad chunk in a GridFS file of the MongoDB storage area");
        }

        const bsoncxx::types::b_binary binary = data.get_binary();
        const int64_t chunkStart = n * chunkSize;
        const int64_t expectedSize = (n == lastChunkOfFile ? fileLength - chunkStart : chunkSize);

        if (static_cast<int64_t>(binary.size) != expectedSize)
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_Database,
                                          "Truncated chunk in a GridFS file of the MongoDB storage area");
        }

        // The part of the chunk that lies in the range
        const int64_t from = std::max(chunkStart, static_cast<int64_t>(start));
        const int64_t to = std::min(chunkStart + expectedSize, end);
        memcpy(&target[static_cast<size_t>(from - static_cast<int64_t>(start))],
               binary.bytes + (from - chunkStart), static_cast<size_t>(to - from));

        expected++;
      }

      if (expected != lastChunk + 1)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database,
                                        "Missing chunk in a GridFS file of the MongoDB storage area");
      }
    }

  public:
    explicit Accessor(MongoDBStorageArea& that) :
      that_(that),
      client_(that.pool_.acquire())
    {
      that_.CheckServer(*client_);
    }

    virtual void Create(const std::string& uuid,
                        const void* content,
                        size_t size,
                        OrthancPluginContentType type) ORTHANC_OVERRIDE
    {
      try
      {
        mongocxx::options::gridfs::upload options;
        options.chunk_size_bytes(that_.chunkSize_);

        mongocxx::gridfs::uploader uploader = GetBucket().open_upload_stream(GetFileName(uuid, type), options);

        try
        {
          if (size > 0)
          {
            uploader.write(reinterpret_cast<const std::uint8_t*>(content), size);
          }

          uploader.close();
        }
        catch (mongocxx::exception&)
        {
          try
          {
            uploader.abort();  // Removes the chunks written so far
          }
          catch (mongocxx::exception&)
          {
          }

          throw;
        }
      }
      catch (mongocxx::exception& e)
      {
        MongoDBDatabase::ThrowException(e);
      }
    }

    virtual void ReadWhole(StorageBackend::IFileContentVisitor& visitor,
                           const std::string& uuid,
                           OrthancPluginContentType type) ORTHANC_OVERRIDE
    {
      std::string content;

      try
      {
        const bsoncxx::document::value file = FindFile(uuid, type);
        Read(content, file.view(), 0, static_cast<uint64_t>(MongoDBToolbox::GetInteger(file.view(), "length")));
      }
      catch (mongocxx::exception& e)
      {
        MongoDBDatabase::ThrowException(e);
      }

      visitor.Assign(content);
    }

    virtual void ReadRange(StorageBackend::IFileContentVisitor& visitor,
                           const std::string& uuid,
                           OrthancPluginContentType type,
                           uint64_t start,
                           size_t length) ORTHANC_OVERRIDE
    {
      std::string content;

      try
      {
        const bsoncxx::document::value file = FindFile(uuid, type);
        Read(content, file.view(), start, length);
      }
      catch (mongocxx::exception& e)
      {
        MongoDBDatabase::ThrowException(e);
      }

      visitor.Assign(content);
    }

    // Removes all the files of the attachment, cf. "FindFile()"
    virtual void Remove(const std::string& uuid,
                        OrthancPluginContentType type) ORTHANC_OVERRIDE
    {
      try
      {
        mongocxx::options::find options;
        options.projection(make_document(kvp("_id", 1)));

        mongocxx::cursor cursor = GetDatabase()[FILES].find(
          make_document(kvp("filename", GetFileName(uuid, type))), options);

        std::vector<bsoncxx::document::value> files;
        for (const bsoncxx::document::view& file : cursor)
        {
          files.push_back(bsoncxx::document::value(file));
        }

        if (files.empty())
        {
          throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource,
                                          "No file in the MongoDB storage area for attachment " + uuid);
        }

        mongocxx::gridfs::bucket bucket = GetBucket();

        for (size_t i = 0; i < files.size(); i++)
        {
          try
          {
            bucket.delete_file(files[i].view()["_id"].get_value());
          }
          catch (mongocxx::gridfs_exception& e)
          {
            // Another thread has just removed it
            if (e.code() != mongocxx::error_code::k_gridfs_file_not_found)
            {
              throw;
            }
          }
        }
      }
      catch (mongocxx::exception& e)
      {
        MongoDBDatabase::ThrowException(e);
      }
    }
  };


  void MongoDBStorageArea::CheckServer(mongocxx::client& client)
  {
    boost::mutex::scoped_lock lock(mutex_);

    if (!serverChecked_)
    {
      try
      {
        bsoncxx::document::value hello = client["admin"].run_command(make_document(kvp("hello", 1)));
        MongoDBDatabase::CheckServerVersion(hello.view());
      }
      catch (mongocxx::exception& e)
      {
        MongoDBDatabase::ThrowException(e);
      }

      serverChecked_ = true;
    }
  }


  MongoDBStorageArea::MongoDBStorageArea(const MongoDBParameters& parameters) :
    StorageBackend(parameters.GetMaxConnectionRetries()),
    databaseName_(parameters.GetDatabaseName()),
    chunkSize_(static_cast<int32_t>(parameters.GetChunkSize())),
    pool_(mongocxx::uri{parameters.GetConnectionUri()}),
    serverChecked_(false)
  {
    if (parameters.GetChunkSize() > static_cast<unsigned int>(std::numeric_limits<int32_t>::max()))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange, "Chunk size too large");
    }
  }


  StorageBackend::IAccessor* MongoDBStorageArea::CreateAccessor()
  {
    try
    {
      return new Accessor(*this);
    }
    catch (mongocxx::exception& e)
    {
      MongoDBDatabase::ThrowException(e);
    }
  }


  std::string MongoDBStorageArea::GetFileName(const std::string& uuid,
                                              OrthancPluginContentType type)
  {
    return uuid + " - " + std::to_string(type);
  }
}
