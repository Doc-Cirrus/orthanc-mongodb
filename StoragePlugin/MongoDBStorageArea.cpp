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

//#include "../Core/PostgreSQLTransaction.h"
//#include "../Core/PostgreSQLResult.h"
#include "../Core/MongoDBException.h"
#include "../Core/Configuration.h"

namespace OrthancPlugins
{  
	MongoDBStorageArea::MongoDBStorageArea(MongoDBConnection* db,
                                               bool useLock,
                                               bool allowUnlock) : 
    db_(db)
    //globalProperties_(*db, useLock, GlobalProperty_StorageLock)
  {
    //globalProperties_.Lock(allowUnlock);

    Prepare();
  }


  void MongoDBStorageArea::Prepare()
  {
	/*
    PostgreSQLTransaction transaction(*db_);

    db_->Execute("CREATE TABLE IF NOT EXISTS StorageArea("
                 "uuid VARCHAR NOT NULL PRIMARY KEY,"
                 "content OID NOT NULL,"
                 "type INTEGER NOT NULL)");

    // Automatically remove the large objects associated with the table
    db_->Execute("CREATE OR REPLACE RULE StorageAreaDelete AS ON DELETE TO StorageArea DO SELECT lo_unlink(old.content);");

    create_.reset(new PostgreSQLStatement(*db_, "INSERT INTO StorageArea VALUES ($1,$2,$3)"));
    create_->DeclareInputString(0);
    create_->DeclareInputLargeObject(1);
    create_->DeclareInputInteger(2);

    read_.reset(new PostgreSQLStatement(*db_, "SELECT content FROM StorageArea WHERE uuid=$1 AND type=$2"));
    read_->DeclareInputString(0);
    read_->DeclareInputInteger(1);

    remove_.reset(new PostgreSQLStatement(*db_, "DELETE FROM StorageArea WHERE uuid=$1 AND type=$2"));
    remove_->DeclareInputString(0);
    remove_->DeclareInputInteger(1);

    transaction.Commit();
	*/
  }


  MongoDBStorageArea::~MongoDBStorageArea()
  {
    //globalProperties_.Unlock();
  }


  void  MongoDBStorageArea::Create(const std::string& uuid,
                                      const void* content,
                                      size_t size,
                                      OrthancPluginContentType type)
  {
	/*
    boost::mutex::scoped_lock lock(mutex_);
    PostgreSQLTransaction transaction(*db_);

    PostgreSQLLargeObject obj(*db_, content, size);
    create_->BindString(0, uuid);
    create_->BindLargeObject(1, obj);    
    create_->BindInteger(2, static_cast<int>(type));    
    create_->Run();

    transaction.Commit();
	*/
  }


  void  MongoDBStorageArea::Read(void*& content,
                                    size_t& size,
                                    const std::string& uuid,
                                    OrthancPluginContentType type) 
  {
	  /*
    boost::mutex::scoped_lock lock(mutex_);
    PostgreSQLTransaction transaction(*db_);

    read_->BindString(0, uuid);
    read_->BindInteger(1, static_cast<int>(type));
    PostgreSQLResult result(*read_);

    if (result.IsDone())
    {
      throw PostgreSQLException();
    }

    result.GetLargeObject(content, size, 0);

    transaction.Commit();
	*/
  }


  void  MongoDBStorageArea::Read(std::string& content,
                                    const std::string& uuid,
                                    OrthancPluginContentType type) 
  {
	  /*
    void* tmp = NULL; 
    size_t size;
    Read(tmp, size, uuid, type);

    try
    {
      content.resize(size);
    }
    catch (std::bad_alloc&)
    {
      free(tmp);
      throw;
    }

    if (size != 0)
    {
      assert(tmp != NULL);
      memcpy(&content[0], tmp, size);
    }

    free(tmp);
	*/
  }


  void  MongoDBStorageArea::Remove(const std::string& uuid,
                                      OrthancPluginContentType type)
  {
	  /*
    boost::mutex::scoped_lock lock(mutex_);
    PostgreSQLTransaction transaction(*db_);

    remove_->BindString(0, uuid);
    remove_->BindInteger(1, static_cast<int>(type));
    remove_->Run();

    transaction.Commit();
	*/
  }


  void MongoDBStorageArea::Clear()
  {
	/*
    boost::mutex::scoped_lock lock(mutex_);
    PostgreSQLTransaction transaction(*db_);

    db_->Execute("DELETE FROM StorageArea");

    transaction.Commit();
	*/
  }

}
