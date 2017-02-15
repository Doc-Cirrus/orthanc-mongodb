/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 DocCirrus, Germany
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General 
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/



#include "MongoDBStorageArea.h"
#include "MongoDBGridFS.h"

#include "../Core/MongoDBException.h"
#include "../Core/Configuration.h"

namespace OrthancPlugins
{  
  MongoDBStorageArea::MongoDBStorageArea(MongoDBConnection* db) : 
  db_(db)
  {
    uri_ = mongoc_uri_new (db->GetConnectionUri().c_str());
    pool_ = mongoc_client_pool_new (uri_);

    mongoc_client_pool_set_error_api (pool_, MONGOC_ERROR_API_VERSION_2);
  }

  MongoDBStorageArea::~MongoDBStorageArea()
  {
    mongoc_client_pool_destroy (pool_);
    mongoc_uri_destroy (uri_);
  }

  void  MongoDBStorageArea::Create(const std::string& uuid,
                                      const void* content,
                                      size_t size,
                                      OrthancPluginContentType type)
  {
    MongoDBGridFS mongoGrid(pool_, uri_, db_->GetChunkSize());
    mongoGrid.SaveFile(uuid, content, size, type);
  }

  void  MongoDBStorageArea::Read(void*& content,
                                    size_t& size,
                                    const std::string& uuid,
                                    OrthancPluginContentType type) 
  {
    MongoDBGridFS mongoGrid(pool_, uri_, db_->GetChunkSize());
    mongoGrid.ReadFile(content, size, uuid, type);
  }

  void  MongoDBStorageArea::Remove(const std::string& uuid,
                                      OrthancPluginContentType type)
  {
    MongoDBGridFS mongoGrid(pool_, uri_, db_->GetChunkSize());
    mongoGrid.RemoveFile(uuid, type);
  }

}
