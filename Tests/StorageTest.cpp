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

#include "gtest/gtest.h"

#include "Configuration.h"
#include "MongoDBConnection.h"
#include "MongoDBStorageArea.h"
#include "MongoDBException.h"

#include <orthanc/OrthancCPlugin.h>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/uri.hpp>

const static char* connection_str = "mongodb://localhost:27017/";
const static char* test_database = "orthanc_mongodb_testdb";

class MongoDBStorageTest : public ::testing::Test {
 protected:
    std::unique_ptr<OrthancPlugins::MongoDBStorageArea> storage_;

  virtual void SetUp() {
    std::unique_ptr<OrthancPlugins::MongoDBConnection> connection = 
            std::make_unique<OrthancPlugins::MongoDBConnection>();
    connection->SetConnectionUri(std::string(connection_str) + test_database);

    // clean DB
    mongocxx::client client{mongocxx::uri{connection->GetConnectionUri()}};
    auto test_db = client[test_database];
    auto collections = test_db.list_collections();
    for (auto&& c : collections)
    {
        std::string name = c["name"].get_utf8().value.to_string();
        test_db[name].drop();
    }

    storage_ = std::make_unique<OrthancPlugins::MongoDBStorageArea>(connection.release());
  }


  // virtual void TearDown() {}

};

const static std::string input_data(1024*1024, 'A');
const static std::string filename = OrthancPlugins::GenerateUuid();
const static OrthancPluginContentType type = OrthancPluginContentType_Unknown;

TEST_F(MongoDBStorageTest, StoreFiles)
{
    storage_->Create(filename, input_data.c_str(), input_data.length(), type);

    void *content;
    size_t size;
    storage_->Read(content, size, filename, type);
    //convert dtaa to string of the specified size
    char* d = static_cast<char *>(content);
    std::string res(d, d + size);

    ASSERT_EQ(input_data.length(), size);
    ASSERT_EQ(input_data, res);

    // free allocated by the Read method memory
    free(content);

    storage_->Remove(filename, type);

    ASSERT_THROW(storage_->Read(content, size, filename, type), OrthancPlugins::MongoDBException);
}

 
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}