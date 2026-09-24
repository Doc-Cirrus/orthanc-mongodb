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


#pragma once

#include "../../Framework/MongoDB/MongoDBIncludes.h"
#include "../../Framework/MongoDB/MongoDBParameters.h"
#include "../../Framework/Plugins/StorageBackend.h"

#include <boost/thread/mutex.hpp>


namespace OrthancDatabases
{
  /**
   * Counterpart of "PostgreSQLStorageArea". Each attachment is a
   * GridFS file of the default bucket ("fs.files" and "fs.chunks"),
   * named "<uuid> - <content type>". All the previous versions of the
   * plugin used the same layout, so their files are read unchanged,
   * whatever their chunk size.
   *
   * Each accessor takes its own client of one "mongocxx::pool", so
   * that the files are read and written concurrently. A range is read
   * from the chunks that hold it only.
   **/
  class MongoDBStorageArea : public StorageBackend
  {
  private:
    class Accessor;

    std::string     databaseName_;
    int32_t         chunkSize_;
    mongocxx::pool  pool_;
    boost::mutex    mutex_;
    bool            serverChecked_;

    // Checks the version of the server once, when the first client connects
    void CheckServer(mongocxx::client& client);

  protected:
    virtual bool HasReadRange() const ORTHANC_OVERRIDE
    {
      return true;
    }

  public:
    explicit MongoDBStorageArea(const MongoDBParameters& parameters);

    virtual IAccessor* CreateAccessor() ORTHANC_OVERRIDE;

    static std::string GetFileName(const std::string& uuid,
                                   OrthancPluginContentType type);
  };
}
