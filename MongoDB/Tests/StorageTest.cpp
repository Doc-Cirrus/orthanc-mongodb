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

#include "gtest/gtest.h"
#include "../Plugins/MongoDBStorageArea.h"

#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>
#include <Toolbox.h>

#include <gtest/gtest.h>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/uri.hpp>

std::string connection_str = "mongodb://host.docker.internal:27017/";
std::string test_database = "test_db_" + Orthanc::Toolbox::GenerateUuid();

class MongoDBStorageTest : public ::testing::Test {
 protected:
    std::unique_ptr<OrthancDatabases::MongoDBStorageArea> storage_;

  virtual void SetUp() 
  {
    storage_ = std::make_unique<OrthancDatabases::MongoDBStorageArea>(std::string(connection_str) + test_database, 261120, 10);
    DropDB();
  }

  void DropDB()
  {
    // drop test DB
    mongocxx::client client{mongocxx::uri{std::string(connection_str) + test_database}};
    client[test_database].drop();
  }

  virtual void TearDown() 
  {
      DropDB();
  }
};

const static std::string input_data(1024*1024, 'A');
const static std::string filename = Orthanc::Toolbox::GenerateUuid();
const static OrthancPluginContentType type = OrthancPluginContentType_Unknown;

TEST_F(MongoDBStorageTest, StoreFiles)
{

    auto *accessor = storage_->CreateAccessor();
    accessor->Create(filename, input_data.c_str(), input_data.length(), type);

    OrthancPluginMemoryBuffer64 *target;
    accessor->ReadWhole(target, filename, type);

    // convert dtaa to string of the specified size
    char* d = static_cast<char *>(target->data);
    std::string res(d, d + target->size);

    ASSERT_EQ(input_data.length(), target->size);
    ASSERT_EQ(input_data, res);

    // free allocated by the Read method memory
    free(target);

    accessor->Remove(filename, type);

    ASSERT_THROW(accessor->ReadWhole(target, filename, type), Orthanc::OrthancException);

    delete accessor;
    accessor = nullptr;
}

 
int main(int argc, char **argv) 
{
  ::testing::InitGoogleTest(&argc, argv);
  Orthanc::Logging::Initialize();
  Orthanc::Logging::EnableInfoLevel(true);
  // Orthanc::Logging::EnableTraceLevel(true);

  int result = RUN_ALL_TESTS();

  Orthanc::Logging::Finalize();

  return result;
}