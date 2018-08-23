/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017  (Doc Cirrus GmbH)   Ronald Wertlen, Ihor Mozil
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



#include "MongoDBConnection.h"

#include <sstream>

namespace OrthancPlugins
{
  
  MongoDBConnection::MongoDBConnection()
  {
  }
 
  void MongoDBConnection::SetConnectionUri(const std::string& uri)
  {
    uri_ = uri;
  }

  std::string MongoDBConnection::GetConnectionUri() const
  {
    if (!uri_.empty())
    {
      return uri_;
    }
    // build from other params
    std::stringstream builder;
    builder << "mongodb://";
    if (!user_.empty())
    {
      builder << user_ << ":" << password_ << "@";
    }
    builder << host_ << ":" << port_ << "/" << database_;
    if (!authenticationDatabase_.empty())
    {
      builder << "?" << "authSource=" << authenticationDatabase_;
    }
    return builder.str();
  }

  void MongoDBConnection::SetChunkSize(int size)
  {
    chunk_size_ = size;
  }

  int MongoDBConnection::GetChunkSize() const
  {
      return chunk_size_;
  }

  std::string MongoDBConnection::GetHost() const
  {
    return host_;
  }

  void MongoDBConnection::SetHost(const std::string& host)
  {
    host_ = host;
  }

  std::string MongoDBConnection::GetDatabase() const
  {
    return database_;
  }

  void MongoDBConnection::SetDatabase(const std::string& database)
  {
    database_ = database;
  }

  int MongoDBConnection::GetTcpPort() const
  {
    return port_;
  }

  void MongoDBConnection::SetTcpPort(int port)
  {
    port_ = port;
  }

  std::string MongoDBConnection::GetUser() const
  {
    return user_;
  }

  void MongoDBConnection::SetUser(const std::string& user)
  {
    user_ = user;
  }

  std::string MongoDBConnection::GetPassword() const
  {
    return password_;
  }

  void MongoDBConnection::SetPassword(const std::string password)
  {
    password_ = password;
  }

  std::string MongoDBConnection::GetAuthenticationDatabase() const
  {
    return authenticationDatabase_;
  }

  void MongoDBConnection::SetAuthenticationDatabase(const std::string authenticationDatabase)
  {
    authenticationDatabase_ = authenticationDatabase;
  }

}
