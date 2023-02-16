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

#pragma once

#include <mongoc.h>
#include <boost/noncopyable.hpp>
#include <orthanc/OrthancCPlugin.h>
#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>

namespace OrthancDatabases {
    class MongoDBStorageArea : public boost::noncopyable {
    private:
        int chunkSize_;
        mongoc_uri_t *uri_;
        mongoc_client_pool_t *pool_;

    public:
        class Accessor : public boost::noncopyable {

        private:
            // does not own that
            mongoc_client_pool_t *pool_;
            mongoc_uri_t *uri_;

            int chunk_size_;

            // Owning the mongo structures
            mongoc_client_t *client_ = nullptr;
            mongoc_gridfs_t *gridfs_ = nullptr;

            void Cleanup() {
                if (gridfs_) mongoc_gridfs_destroy(gridfs_);
                if (client_) mongoc_client_pool_push(pool_, client_);
            };

            mongoc_gridfs_file_t *CreateMongoDBFile(const std::string &uuid,
                                                    OrthancPluginContentType type, bool createFile);

            static mongoc_stream_t *CreateMongoDBStream(mongoc_gridfs_file_t *file);

        public:
            explicit Accessor(mongoc_client_pool_t *pool, mongoc_uri_t *uri, int chunk_size) : pool_(pool), uri_(uri),
                                                                                               chunk_size_(chunk_size) {
                const char *database_name = mongoc_uri_get_database(uri_);
                if (!database_name) {
                    Cleanup();
                    LOG(ERROR) << "MongoDBGridFS::MongoDBGridFS - Cannot not parse mongodb URI.";
                    throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
                }
                client_ = mongoc_client_pool_pop(pool_);
                if (!client_) {
                    Cleanup();
                    LOG(ERROR) << "MongoDBGridFS::MongoDBGridFS - Cannot initialize mongodb client.";
                    throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
                }

                // create a mongoc_gridfs_t interface
                gridfs_ = mongoc_client_get_gridfs(client_, database_name, nullptr, nullptr);

                if (!gridfs_) {
                    Cleanup();
                    LOG(ERROR) << "MongoDBGridFS::MongoDBGridFS - Cannot initialize mongoc gridfs.";
                    throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
                };
            }

            virtual ~Accessor() {
                Cleanup();
            };

            virtual void Create(const std::string &uuid,
                                const void *content,
                                size_t size,
                                OrthancPluginContentType type);

            virtual void ReadWhole(OrthancPluginMemoryBuffer64 *target,
                                   const std::string &uuid,
                                   OrthancPluginContentType type);

            virtual void ReadRange(OrthancPluginMemoryBuffer64 *target,
                                   const std::string &uuid,
                                   OrthancPluginContentType type,
                                   uint64_t rangeStart);

            virtual void Remove(const std::string &uuid, OrthancPluginContentType type);
        };

        explicit MongoDBStorageArea(const std::string &url, const int &chunkSize, const int &maxConnectionRetries);

        ~MongoDBStorageArea();

        static void Register(OrthancPluginContext *context, MongoDBStorageArea *backend);   // Takes ownership

        static void Finalize();

        virtual Accessor* CreateAccessor() {
            return new Accessor(pool_, uri_, chunkSize_);
        }
    };
}
