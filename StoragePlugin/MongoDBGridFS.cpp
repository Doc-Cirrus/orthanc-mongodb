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



#include "MongoDBGridFS.h"


namespace OrthancPlugins
{
    MongoDBGridFS::MongoDBGridFS(mongoc_client_pool_t *pool, mongoc_uri_t *uri) :
        pool_(pool),
        uri_(uri)
    {
        const char *database_name = mongoc_uri_get_database (uri_);
        if (!database_name) 
        {
            TearDown();
            throw MongoDBException("Is not set");
        }
        client_ = mongoc_client_pool_pop (pool_);
        if (!client_)
        {
            TearDown();
            throw MongoDBException("Could not initialize mongodb client");
        }

        bson_error_t error;
        gridfs_ = mongoc_client_get_gridfs (client_, database_name ? database_name : "orthanc", "fs", &error);
        if (!gridfs_) 
        {
            TearDown();
            throw MongoDBException("Is not set");
        }
    }

    void MongoDBGridFS::TearDown() 
    {
        if (stream_) 
        {
            mongoc_stream_destroy(stream_);
        }
        if (file_) 
        {
            mongoc_gridfs_file_destroy(file_);
        }
        if (gridfs_) 
        {
            mongoc_gridfs_destroy (gridfs_);
        }
        if (client_) 
        {
            mongoc_client_pool_push (pool_, client_);
        }
    }

    MongoDBGridFS::~MongoDBGridFS() 
    {
        TearDown();
    }

    void MongoDBGridFS::SaveFile(const std::string& uuid,
                    const void* content,
                    size_t size,
                    OrthancPluginContentType type) {
        CreateMongoDBFile(uuid, type, true);

        mongoc_iovec_t iov;
        iov.iov_len = size;
#ifdef _WIN32
        iov.iov_base = const_cast<void *>(content);
#else
        iov.iov_base = static_cast<char *>(const_cast<void *>(content));
#endif
        mongoc_stream_writev(stream_, &iov, 1, 0);
        if (!mongoc_gridfs_file_save(file_))
        {
            throw MongoDBException("Could write file.");
        }
    }

    void MongoDBGridFS::ReadFile(void*& content,
                                    size_t& size,
                                    const std::string& uuid,
                                    OrthancPluginContentType type) {
        CreateMongoDBFile(uuid, type, false);
        
        size = mongoc_gridfs_file_get_length(file_);
        if (0 == size)
        {
            content = NULL;
        } else
        {
            content = malloc(size);
        }
        
        mongoc_iovec_t iov;
        iov.iov_len = size;
#ifdef _WIN32
        iov.iov_base = static_cast<char *>(content);
#else
        iov.iov_base = content;
#endif
        mongoc_stream_readv(stream_, &iov, 1, -1, 0);
    }

    void MongoDBGridFS::RemoveFile(const std::string& uuid,
                                    OrthancPluginContentType type) {
        CreateMongoDBFile(uuid, type, false);
        bson_error_t error;
        mongoc_gridfs_file_remove(file_, &error);
    }

    void MongoDBGridFS::CreateMongoDBFile(const std::string& uuid,
                                    OrthancPluginContentType type, bool createFile = true) {
        char file_name[1024];
        sprintf(file_name, "%s - %i", uuid.c_str(), type);
        if (createFile)
        {
            mongoc_gridfs_file_opt_t opt = { 0 };
            opt.filename = file_name;
            file_ = mongoc_gridfs_create_file(gridfs_, &opt);
        } else
        {
            bson_error_t error;
            file_ = mongoc_gridfs_find_one_by_filename(gridfs_, file_name, &error);
        }

        if (!file_)
        {
            throw MongoDBException("Could not create file.");
        }
        stream_ = mongoc_stream_gridfs_new(file_);
        if (!stream_)
        {
            throw MongoDBException("Could not create stream.");
        }
    }
  

}