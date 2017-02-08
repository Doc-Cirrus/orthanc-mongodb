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


#pragma once

#include <mongoc.h>
#include <boost/core/noncopyable.hpp>
#include <orthanc/OrthancCPlugin.h>

#include "MongoDBException.h"

namespace OrthancPlugins
{  
  class MongoDBGridFS : public boost::noncopyable
  {
  private:
    // does not own that
    mongoc_client_pool_t *pool_;
    mongoc_uri_t *uri_;

    // Owning the mongo structures
    mongoc_client_t *client_ = NULL;
    mongoc_gridfs_t *gridfs_ = NULL;
    mongoc_gridfs_file_t *file_ = NULL;
    mongoc_stream_t *stream_ = NULL;

    mongoc_gridfs_file_opt_t opt = { 0 };
    bson_error_t error;

    void TearDown();
    void CreateMongoDBFile(const std::string& uuid,
                                    OrthancPluginContentType type, bool createFile);

  public:
    MongoDBGridFS(mongoc_client_pool_t *pool, mongoc_uri_t *uri);
    ~MongoDBGridFS();

    void SaveFile(const std::string& uuid,
                    const void* content,
                    size_t size,
                    OrthancPluginContentType type);

     void ReadFile(void*& content,
                    size_t& size,
                    const std::string& uuid,
                    OrthancPluginContentType type);

     void RemoveFile(const std::string& uuid,
                    OrthancPluginContentType type);
 };
}