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

#include "../Core/MongoDBException.h"
#include "../Core/Configuration.h"

namespace OrthancPlugins
{  
	MongoDBStorageArea::MongoDBStorageArea(MongoDBConnection* db) : 
    db_(db)
  {
  }

  MongoDBStorageArea::~MongoDBStorageArea()
  {
  }

  void  MongoDBStorageArea::Create(const std::string& uuid,
                                      const void* content,
                                      size_t size,
                                      OrthancPluginContentType type)
  {
  }

  void  MongoDBStorageArea::Read(void*& content,
                                    size_t& size,
                                    const std::string& uuid,
                                    OrthancPluginContentType type) 
  {
  }

  void  MongoDBStorageArea::Read(std::string& content,
                                    const std::string& uuid,
                                    OrthancPluginContentType type) 
  {
  }

  void  MongoDBStorageArea::Remove(const std::string& uuid,
                                      OrthancPluginContentType type)
  {
  }

  void MongoDBStorageArea::Clear()
  {
  }

}
