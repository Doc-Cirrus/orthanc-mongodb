/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2023  (Doc Cirrus GmbH)
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

#if HAS_ORTHANC_EXCEPTION != 1
#  error HAS_ORTHANC_EXCEPTION must be set to 1
#endif

#include "MongoDBStorageArea.h"

#include <bson.h>

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>


#define ORTHANC_PLUGINS_DATABASE_CATCH                                  \
  catch (::Orthanc::OrthancException& e)                                \
  {                                                                     \
    return static_cast<OrthancPluginErrorCode>(e.GetErrorCode());       \
  }                                                                     \
  catch (::std::runtime_error& e)                                       \
  {                                                                     \
    std::string s = "Exception in storage area back-end: " + std::string(e.what()); \
    OrthancPluginLogError(context_, s.c_str());                         \
    return OrthancPluginErrorCode_DatabasePlugin;                       \
  }                                                                     \
  catch (...)                                                           \
  {                                                                     \
    OrthancPluginLogError(context_, "Native exception");                \
    return OrthancPluginErrorCode_DatabasePlugin;                       \
  }

namespace OrthancDatabases {
    // overrides
    mongoc_gridfs_file_t *MongoDBStorageArea::Accessor::CreateMongoDBFile(
            mongoc_gridfs_t *gridfs,
            const std::string &uuid,
            OrthancPluginContentType type, 
            bool createFile = true
    ) {
        mongoc_gridfs_file_t *file;

        if (createFile) {
            auto filename = uuid + " - " + std::to_string(type);
            mongoc_gridfs_file_opt_t options = {nullptr};
            options.chunk_size = chunk_size_;
            options.filename = filename.c_str();

            file = mongoc_gridfs_create_file(gridfs, &options);
        } else {
            bson_t *filter;

            filter = BCON_NEW ("filename", "{", "$regex", BCON_UTF8(uuid.c_str()), "}");
            file = mongoc_gridfs_find_one_with_opts(gridfs, filter, nullptr, nullptr);

            bson_destroy(filter);
        }

        if (!file) {
            LOG(ERROR) << "MongoDBGridFS::CreateMongoDBFile - Could not create file.";
            throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource);
        }
        return file;
    }

    mongoc_stream_t *MongoDBStorageArea::Accessor::CreateMongoDBStream(mongoc_gridfs_file_t *file) {
        mongoc_stream_t *stream = mongoc_stream_gridfs_new(file);

        if (!stream) {
            LOG(ERROR) << "MongoDBGridFS::CreateMongoDBStream - Could not create stream.";
            throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
        }

        return stream;
    }

    void MongoDBStorageArea::Accessor::Create(const std::string &uuid,
                                              const void *content,
                                              size_t size,
                                              OrthancPluginContentType type) {
        mongoc_client_t *client = PopClient();
        mongoc_gridfs_t *gridfs = mongoc_client_get_gridfs(client, database_name_, nullptr, nullptr);

        mongoc_gridfs_file_t *file = CreateMongoDBFile(gridfs, uuid, type, true);
        mongoc_stream_t *stream = CreateMongoDBStream(file);

        mongoc_iovec_t iov;
        iov.iov_len = size;
        iov.iov_base = const_cast<void *>(content);

        mongoc_stream_writev(stream, &iov, 1, 0);
        if (!mongoc_gridfs_file_save(file)) {
            LOG(ERROR) << "MongoDBStorageArea::Accessor::Create - Could write file.";
            throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
        }

        mongoc_stream_destroy(stream);
        mongoc_gridfs_file_destroy(file);
        mongoc_gridfs_destroy(gridfs);

        PushClient(client);
    }

    void MongoDBStorageArea::Accessor::ReadWhole(OrthancPluginMemoryBuffer64 *target,
                                                 const std::string &uuid,
                                                 OrthancPluginContentType type) {
        LOG(ERROR) << "READ WHOLE START - " << to_iso_extended_string(boost::posix_time::microsec_clock::universal_time());

        mongoc_client_t *client = PopClient();
        mongoc_gridfs_t *gridfs = mongoc_client_get_gridfs(client, database_name_, nullptr, nullptr);

        mongoc_gridfs_file_t *file = CreateMongoDBFile(gridfs, uuid, type, false);
        mongoc_stream_t *stream = CreateMongoDBStream(file);

        target->size = mongoc_gridfs_file_get_length(file);
        target->data = (0 == target->size) ? nullptr : malloc(target->size);

        mongoc_stream_read(stream, target->data, target->size, -1, 0);

        mongoc_stream_destroy(stream);
        mongoc_gridfs_file_destroy(file);
        mongoc_gridfs_destroy(gridfs);

        PushClient(client);

        LOG(ERROR) << "READ WHOLE END - " << to_iso_extended_string(boost::posix_time::microsec_clock::universal_time());
    };

    void MongoDBStorageArea::Accessor::ReadRange(OrthancPluginMemoryBuffer64 *target,
                                                 const std::string &uuid,
                                                 OrthancPluginContentType type,
                                                 uint64_t rangeStart) {

        LOG(ERROR) << "READ RANGE START - " << to_iso_extended_string(boost::posix_time::microsec_clock::universal_time());
        mongoc_client_t *client = PopClient();
        mongoc_gridfs_t *gridfs = mongoc_client_get_gridfs(client, database_name_, nullptr, nullptr);

        mongoc_gridfs_file_t *file = CreateMongoDBFile(gridfs, uuid, type, false);
        mongoc_gridfs_file_seek(file, static_cast<int64_t>(rangeStart), SEEK_SET);

        mongoc_stream_t *stream = CreateMongoDBStream(file);

        mongoc_iovec_t iov;
        iov.iov_len = target->size;
        iov.iov_base = malloc(target->size);

        mongoc_stream_read(stream, iov.iov_base, target->size, -1, 0);
        memcpy(target->data, iov.iov_base, target->size);

        mongoc_stream_destroy(stream);
        mongoc_gridfs_file_destroy(file);
        mongoc_gridfs_destroy(gridfs);

        PushClient(client);

        LOG(ERROR) << "READ RANGE END - " << to_iso_extended_string(boost::posix_time::microsec_clock::universal_time());
    };

    void MongoDBStorageArea::Accessor::Remove(const std::string &uuid, OrthancPluginContentType type) {
        bson_error_t error;
        mongoc_client_t *client = PopClient();
        mongoc_gridfs_t *gridfs = mongoc_client_get_gridfs(client, database_name_, nullptr, nullptr);

        mongoc_gridfs_file_t *file = CreateMongoDBFile(gridfs, uuid, type, false);

        bool r = mongoc_gridfs_file_remove(file, nullptr);
        if (r) mongoc_gridfs_file_destroy(file);
        else {
            LOG(ERROR) << "MongoDBStorageArea::Accessor::Remove - Could not remove file: " << std::string(error.message);
            throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
        }

        mongoc_gridfs_destroy(gridfs);
        PushClient(client);
    };

    MongoDBStorageArea::MongoDBStorageArea(const std::string &url, const int &chunkSize,
                                           const int &maxConnectionRetries) :
            chunkSize_(chunkSize) {
        uri_ = mongoc_uri_new(url.c_str());
        pool_ = mongoc_client_pool_new(uri_);

        mongoc_client_pool_set_error_api(pool_, MONGOC_ERROR_API_VERSION_2);
    }

    MongoDBStorageArea::~MongoDBStorageArea() {
        mongoc_client_pool_destroy(pool_);
        mongoc_uri_destroy(uri_);
    }

    static OrthancPluginContext *context_ = nullptr;
    static std::unique_ptr<MongoDBStorageArea> backend_;

    static OrthancPluginErrorCode StorageCreate(const char *uuid,
                                                const void *content,
                                                int64_t size,
                                                OrthancPluginContentType type) {
        try {
            MongoDBStorageArea::Accessor *accessor = backend_->CreateAccessor();
            accessor->Create(uuid, content, size, type);

            delete accessor;
            accessor = nullptr;

            return OrthancPluginErrorCode_Success;
        }
        ORTHANC_PLUGINS_DATABASE_CATCH;
    }


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 9, 0)

    static OrthancPluginErrorCode StorageReadWhole(OrthancPluginMemoryBuffer64 *target,
                                                   const char *uuid,
                                                   OrthancPluginContentType type) {
        try {
            MongoDBStorageArea::Accessor *accessor = backend_->CreateAccessor();
            accessor->ReadWhole(target, uuid, type);

            delete accessor;
            accessor = nullptr;

            return OrthancPluginErrorCode_Success;
        }
        ORTHANC_PLUGINS_DATABASE_CATCH;
    }


    static OrthancPluginErrorCode StorageReadRange(OrthancPluginMemoryBuffer64 *target,
                                                   const char *uuid,
                                                   OrthancPluginContentType type,
                                                   uint64_t start) {
        try {
            MongoDBStorageArea::Accessor *accessor = backend_->CreateAccessor();
            accessor->ReadRange(target, uuid, type, start);

            delete accessor;
            accessor = nullptr;

            return OrthancPluginErrorCode_Success;
        }
        ORTHANC_PLUGINS_DATABASE_CATCH;
    }

#  endif
#endif


    static OrthancPluginErrorCode StorageRead(void **data,
                                              int64_t *size,
                                              const char *uuid,
                                              OrthancPluginContentType type) {
        try {
            // not implemented now
            return OrthancPluginErrorCode_Success;
        }
        ORTHANC_PLUGINS_DATABASE_CATCH;
    }


    static OrthancPluginErrorCode StorageRemove(const char *uuid,
                                                OrthancPluginContentType type) {
        try {
            MongoDBStorageArea::Accessor *accessor = backend_->CreateAccessor();
            accessor->Remove(uuid, type);

            delete accessor;
            accessor = nullptr;

            return OrthancPluginErrorCode_Success;
        }
        ORTHANC_PLUGINS_DATABASE_CATCH;
    }

    void MongoDBStorageArea::Register(OrthancPluginContext *context, MongoDBStorageArea *backend) {
        if (context == nullptr || backend == nullptr) {
            throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
        } else if (context_ != nullptr || backend_.get() != nullptr) {
            // This function can only be invoked once in the plugin
            throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
        } else {
            context_ = context;
            backend_.reset(backend);

            bool hasLoadedV2 = false;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)         // Macro introduced in Orthanc 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 9, 0)
            if (OrthancPluginCheckVersionAdvanced(context, 1, 9, 0) == 1) {
                OrthancPluginRegisterStorageArea2(context_, StorageCreate, StorageReadWhole, StorageReadRange,
                                                  StorageRemove);
                hasLoadedV2 = true;
            }
#  endif
#endif

            if (!hasLoadedV2) {
                LOG(WARNING)
                        << "Performance warning: Your version of the Orthanc core or SDK doesn't support reading of file ranges";
                OrthancPluginRegisterStorageArea(context_, StorageCreate, StorageRead, StorageRemove);
            }
        }
    }

    void MongoDBStorageArea::Finalize() {
        backend_.reset(nullptr);
        context_ = nullptr;
    }
}
