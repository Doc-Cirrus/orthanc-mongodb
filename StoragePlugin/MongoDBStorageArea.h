/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017 Osimis, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#pragma once

//#include "../Core/GlobalProperties.h"
#include "../Core/MongoDBConnection.h"
//#include "../Core/MongoDBStatement.h"

#include <orthanc/OrthancCPlugin.h>
#include <memory>
#include <boost/thread/mutex.hpp>

namespace OrthancPlugins
{  
  class MongoDBStorageArea
  {
  private:
    std::unique_ptr<MongoDBConnection>  db_;
    //GlobalProperties globalProperties_;

    boost::mutex mutex_;
    //std::unique_ptr<PostgreSQLStatement>  create_;
    //std::unique_ptr<PostgreSQLStatement>  read_;
    //std::unique_ptr<PostgreSQLStatement>  remove_;

    void Prepare();

  public:
	 MongoDBStorageArea(MongoDBConnection* db,   // Takes the ownership
                          bool useLock,
                          bool allowUnlock);

    ~MongoDBStorageArea();

    void Create(const std::string& uuid,
                const void* content,
                size_t size,
                OrthancPluginContentType type);

    void Read(std::string& content,
              const std::string& uuid,
              OrthancPluginContentType type);

    void Read(void*& content,
              size_t& size,
              const std::string& uuid,
              OrthancPluginContentType type);

    void Remove(const std::string& uuid,
                OrthancPluginContentType type);

    void Clear();

    // For unit tests only (not thread-safe)!
	MongoDBConnection& GetConnection()
    {
      return *db_;
    }
  };
}
