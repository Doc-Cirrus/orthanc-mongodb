/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017  (Doc Cirrus GmbH)   Ronald Wertlen, Ihor Mozil
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

//#include "../Core/GlobalProperties.h"
#include "../Core/MongoDBConnection.h"
//#include "../Core/MongoDBStatement.h"

#include <orthanc/OrthancCPlugin.h>
#include <memory>
#include <mutex>
#include <mongoc.h>

namespace OrthancPlugins
{  
  class MongoDBStorageArea
  {
  private:
    std::unique_ptr<MongoDBConnection>  db_;
    std::mutex mutex_;
    mongoc_client_pool_t *pool_;
    mongoc_uri_t *uri_;

  public:
    MongoDBStorageArea(MongoDBConnection* db);
    ~MongoDBStorageArea();

    void Create(const std::string& uuid,
                const void* content,
                size_t size,
                OrthancPluginContentType type);

    void Read(void*& content,
              size_t& size,
              const std::string& uuid,
              OrthancPluginContentType type);

    void Remove(const std::string& uuid,
                OrthancPluginContentType type);

    // For unit tests only (not thread-safe)!
  MongoDBConnection& GetConnection()
  {
    return *db_;
  }
  };
}
