/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2020  (Doc Cirrus GmbH)   Ihor Mozil, Andrii Dubyk
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

#include "gtest/gtest.h"

#include "Configuration.h"
#include "MongoDBConnection.h"

#include <orthanc/OrthancCPlugin.h>

#include <json/reader.h>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/uri.hpp>

static int DatabaseVersion = GlobalProperty_DatabaseSchemaVersion;

OrthancPluginErrorCode PluginServiceMock(struct _OrthancPluginContext_t *context,
                                         _OrthancPluginService service,
                                         const void *params)
{
  if (service == _OrthancPluginService_GetExpectedDatabaseVersion)
  {
    _OrthancPluginReturnSingleValue *p = static_cast<_OrthancPluginReturnSingleValue *>(
        const_cast<void *>(params));
    (*p->resultUint32) = DatabaseVersion;
  }

  return OrthancPluginErrorCode_Success;
}

class ConfigurationTest : public ::testing::Test
{
protected:
  std::unique_ptr<OrthancPluginContext> context_;

  virtual void SetUp()
  {
    context_ = std::make_unique<OrthancPluginContext>();
    context_->InvokeService = &PluginServiceMock;
  }
};

TEST_F(ConfigurationTest, Configuration)
{
  std::string conf_str = R"(
        {
            "MongoDB" : {
                "host" : "customhost",
                "port" : 27001,
                "user" : "user",
                "database" : "database",
                "password" : "password",
                "authenticationDatabase" : "admin",
                "ChunkSize" : 1000
            }
        }
    )";

  Json::Reader r;
  Json::Value v;
  r.parse(conf_str, v);

  std::unique_ptr<OrthancPlugins::MongoDBConnection> connection =
      std::unique_ptr<OrthancPlugins::MongoDBConnection>(OrthancPlugins::CreateConnection(context_.get(), v));

  ASSERT_EQ("mongodb://user:password@customhost:27001/database?authSource=admin", connection->GetConnectionUri());
  ASSERT_EQ(connection->GetHost(), "customhost");
  ASSERT_EQ(connection->GetTcpPort(), 27001);
  ASSERT_EQ(connection->GetUser(), "user");
  ASSERT_EQ(connection->GetPassword(), "password");
  ASSERT_EQ(connection->GetDatabase(), "database");
  ASSERT_EQ(connection->GetAuthenticationDatabase(), "admin");
  ASSERT_EQ(connection->GetChunkSize(), 1000);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
