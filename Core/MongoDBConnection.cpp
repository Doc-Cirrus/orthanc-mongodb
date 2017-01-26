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


#include "MongoDBConnection.h"

//#include "PostgreSQLException.h"
//#include "PostgreSQLResult.h"
//#include "PostgreSQLStatement.h"
//#include "PostgreSQLTransaction.h"

#include <boost/lexical_cast.hpp>

// PostgreSQL includes
//#include <libpq-fe.h>
//#include <c.h>
//#include <catalog/pg_type.h>


namespace OrthancPlugins
{
  void MongoDBConnection::Close()
  {
    if (pg_ != NULL)
    {
      //PQfinish(reinterpret_cast<PGconn*>(pg_));
      pg_ = NULL;
    }
  }

  
  MongoDBConnection::MongoDBConnection()
  {
    pg_ = NULL;
    host_ = "localhost";
    port_ = 5432;
    username_ = "postgres";
    password_ = "postgres";
    database_ = "";
    uri_.clear();
  }

  
  MongoDBConnection::MongoDBConnection(const MongoDBConnection& other) :
    host_(other.host_),
    port_(other.port_),
    username_(other.username_),
    password_(other.password_),
    database_(other.database_),
    pg_(NULL)
  {
  }


  void MongoDBConnection::SetConnectionUri(const std::string& uri)
  {
    Close();
    uri_ = uri;
  }


  std::string MongoDBConnection::GetConnectionUri() const
  {
    if (uri_.empty())
    {
      return ("postgresql://" + username_ + ":" + password_ + "@" + 
              host_ + ":" + boost::lexical_cast<std::string>(port_) + "/" + database_);
    }
    else
    {
      return uri_;
    }
  }


  void MongoDBConnection::SetHost(const std::string& host)
  {
    Close();
    uri_.clear();
    host_ = host;
  }

  void MongoDBConnection::SetPortNumber(uint16_t port)
  {
    Close();
    uri_.clear();
    port_ = port;
  }

  void MongoDBConnection::SetUsername(const std::string& username)
  {
    Close();
    uri_.clear();
    username_ = username;
  }

  void MongoDBConnection::SetPassword(const std::string& password)
  {
    Close();
    uri_.clear();
    password_ = password;
  }

  void MongoDBConnection::SetDatabase(const std::string& database)
  {
    Close();
    uri_.clear();
    database_ = database;
  }

  void MongoDBConnection::Open()
  {
    
  }


  void MongoDBConnection::Execute(const std::string& sql)
  {
    
  }


  bool MongoDBConnection::DoesTableExist(const char* name)
  {
    
    return false;
  }



  void MongoDBConnection::ClearAll()
  {
    
  }

}
