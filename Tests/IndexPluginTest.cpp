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
#include "MongoDBBackend.h"

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

static OrthancPluginContext static_context;
static OrthancPlugins::MongoDBConnection connection;


OrthancPluginErrorCode PluginServiceMock(struct _OrthancPluginContext_t* context,
                                              _OrthancPluginService service,
                                              const void* params)
{
    if (service == _OrthancPluginService_GetExpectedDatabaseVersion) {
        _OrthancPluginReturnSingleValue *p = static_cast<_OrthancPluginReturnSingleValue *>(
                                                    const_cast<void *>(params));
        (*p->resultUint32) = 6;
    }
    else if(service == _OrthancPluginService_LogInfo) {
        const char *data = static_cast<const char *>(params);
        std::cout << data << std::endl;
    }
    else
    {
        std::cout << "Unknown type: " << service << std::endl;
    }
    return OrthancPluginErrorCode_Success;
}


class MongoDBBackendTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    static_context.InvokeService = &PluginServiceMock;
    connection.SetConnectionUri("mongodb://localhost:27017/test");
  }

  // virtual void TearDown() {}

};

TEST_F (MongoDBBackendTest, GetTotalCompressedSize) {
    OrthancPlugins::MongoDBBackend backend(&static_context, &connection);
    size_t size = backend.GetTotalCompressedSize();
    std::cout << "Total size: " << size << std::endl;
    ASSERT_GT(size, 0);
}

TEST_F (MongoDBBackendTest, ProtectedPatient) {
    OrthancPlugins::MongoDBBackend backend(&static_context, &connection);

    int64_t pId = 1001;

    bool isProtected = backend.IsProtectedPatient(pId);
    ASSERT_EQ(isProtected, false);

    backend.SetProtectedPatient(pId, true);
    isProtected = backend.IsProtectedPatient(pId);
    ASSERT_EQ(isProtected, true);

    backend.SetProtectedPatient(pId, false);
    isProtected = backend.IsProtectedPatient(pId);
    ASSERT_EQ(isProtected, false);
    
//    ASSERT_EQ (-1, -1);
}
 
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
