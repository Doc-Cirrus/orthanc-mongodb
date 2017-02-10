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
    MongoDBGridFS::MongoDBGridFS(mongoc_client_pool_t *pool, mongoc_uri_t *uri, int chunk_size) :
        pool_(pool),
        uri_(uri),
        chunk_size_(chunk_size)
    {
        const char *database_name = mongoc_uri_get_database (uri_);
        if (!database_name) 
        {
            Cleanup();
            throw MongoDBException("Is not set");
        }
        client_ = mongoc_client_pool_pop (pool_);
        if (!client_)
        {
            Cleanup();
            throw MongoDBException("Could not initialize mongodb client");
        }

        bson_error_t error;
        gridfs_ = mongoc_client_get_gridfs (client_, database_name ? database_name : "orthanc", "fs", &error);
        if (!gridfs_) 
        {
            Cleanup();
            throw MongoDBException("Is not set");
        }
    }

    void MongoDBGridFS::Cleanup() 
    {
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
        Cleanup();
    }

    void MongoDBGridFS::SaveFile(const std::string& uuid,
                    const void* content,
                    size_t size,
                    OrthancPluginContentType type) {
        mongoc_gridfs_file_t *file = CreateMongoDBFile(uuid, type, true);
        mongoc_stream_t *stream = CreateMongoDBStream(file);

        mongoc_iovec_t iov;
        iov.iov_len = size;
#ifdef _WIN32
        iov.iov_base = static_cast<char *>(const_cast<void *>(content));
#else
        iov.iov_base = const_cast<void *>(content);
#endif
        mongoc_stream_writev(stream, &iov, 1, 0);
        if (!mongoc_gridfs_file_save(file))
        {
            throw MongoDBException("Could write file.");
        }
        mongoc_stream_destroy(stream);
        mongoc_gridfs_file_destroy(file);
    }

    void MongoDBGridFS::ReadFile(void*& content,
                                    size_t& size,
                                    const std::string& uuid,
                                    OrthancPluginContentType type)
    {
        mongoc_gridfs_file_t *file = CreateMongoDBFile(uuid, type, false);
        mongoc_stream_t *stream = CreateMongoDBStream(file);
        
        size = mongoc_gridfs_file_get_length(file);
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
        mongoc_stream_readv(stream, &iov, 1, -1, 0);

        mongoc_stream_destroy(stream);
        mongoc_gridfs_file_destroy(file);
    }

    void MongoDBGridFS::RemoveFile(const std::string& uuid,
                                    OrthancPluginContentType type) {
        mongoc_gridfs_file_t *file = CreateMongoDBFile(uuid, type, false);
        bson_error_t error;
        mongoc_gridfs_file_remove(file, &error);
        mongoc_gridfs_file_destroy(file);
    }

    mongoc_gridfs_file_t *MongoDBGridFS::CreateMongoDBFile(const std::string& uuid,
                                    OrthancPluginContentType type, bool createFile = true) {
        mongoc_gridfs_file_t *file = NULL;
        char file_name[1024];
        sprintf(file_name, "%s - %i", uuid.c_str(), type);
        if (createFile)
        {
            mongoc_gridfs_file_opt_t opt = { 0 };
            opt.filename = file_name;
            opt.chunk_size = chunk_size_;
            file = mongoc_gridfs_create_file(gridfs_, &opt);
        } else
        {
            bson_error_t error;
            file = mongoc_gridfs_find_one_by_filename(gridfs_, file_name, &error);
        }

        if (!file)
        {
            throw MongoDBException("Could not create file.");
        }
        return file;
    }

    mongoc_stream_t *MongoDBGridFS::CreateMongoDBStream(mongoc_gridfs_file_t *file)
    {
        mongoc_stream_t *stream = NULL;
        stream = mongoc_stream_gridfs_new(file);
        if (!stream)
        {
            throw MongoDBException("Could not create stream.");
        }
        return stream;
    }
  

}