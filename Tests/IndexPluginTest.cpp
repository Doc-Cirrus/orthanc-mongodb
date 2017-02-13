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

#include <orthanc/OrthancCPlugin.h>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/uri.hpp>

const static char* connection_str = "mongodb://localhost:27017/";
const static char* test_database = "orthanc_mongodb_testdb";

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

    std::unique_ptr<OrthancPluginContext> context_;
    std::unique_ptr<OrthancPlugins::MongoDBConnection> connection_;
    std::unique_ptr<OrthancPlugins::MongoDBBackend> backend_;
    OrthancPlugins::DatabaseBackendOutput *output_; // IDatabaseBackend delete the registered output in the desctructor.

    OrthancPluginDatabaseContext *db_context_ = NULL;

  virtual void SetUp() {
    context_ = std::make_unique<OrthancPluginContext>();
    context_->InvokeService = &PluginServiceMock;

    connection_ = std::make_unique<OrthancPlugins::MongoDBConnection>();
    connection_->SetConnectionUri(std::string(connection_str) + test_database);

    backend_ = std::make_unique<OrthancPlugins::MongoDBBackend>(context_.get(), connection_.get());
    output_ = new OrthancPlugins::DatabaseBackendOutput(context_.get(), db_context_);
    backend_->RegisterOutput(output_);

    // clean DB
    mongocxx::client client{mongocxx::uri{connection_->GetConnectionUri()}};
    auto test_db = client[test_database];
    auto collections = test_db.list_collections();
    for (auto&& c : collections)
    {
        std::string name = c["name"].get_utf8().value.to_string();
        test_db[name].drop();
    }

  }

  // virtual void TearDown() {}

};

OrthancPluginAttachment attachment {
    "", //const char* uuid;
    0, //int32_t     contentType;
    100, //uint64_t    uncompressedSize;
    "", //const char* uncompressedHash;
    0, //int32_t     compressionType;
    100, //uint64_t    compressedSize;
    "" //const char* compressedHash;
};

TEST_F(MongoDBBackendTest, Attachments) 
{
    backend_->AddAttachment(0, attachment);
    bool found = backend_->LookupAttachment(0, 0);
    ASSERT_TRUE(found);

    size_t size = backend_->GetTotalCompressedSize();
    ASSERT_GT(size, 0);

    size = backend_->GetTotalUncompressedSize();
    ASSERT_GT(size, 0);

    std::list<int32_t> list;
    backend_->ListAvailableAttachments(list, 0);
    ASSERT_EQ(1, list.size());

    backend_->DeleteAttachment(0, 0);
    found = backend_->LookupAttachment(0, 0);
    ASSERT_FALSE(found);
}

TEST_F (MongoDBBackendTest, Resource) 
{
    int64_t id = backend_->CreateResource("", OrthancPluginResourceType_Patient);
    ASSERT_TRUE(id > 0);

    std::list<int64_t> list;
    backend_->GetAllInternalIds(list, OrthancPluginResourceType_Patient);
    ASSERT_EQ(1, list.size());
    ASSERT_EQ(id, *list.begin());

    std::list<std::string> list1;
    backend_->GetAllPublicIds(list1, OrthancPluginResourceType_Patient);
    ASSERT_EQ(1, list.size());
    ASSERT_EQ("", *list1.begin());

    std::string pId = backend_->GetPublicId(id);
    ASSERT_EQ(pId, "");

    uint64_t count = backend_->GetResourceCount(OrthancPluginResourceType_Patient);
    ASSERT_EQ(count, 1);

    OrthancPluginResourceType rt = backend_->GetResourceType(id);
    ASSERT_EQ(rt, OrthancPluginResourceType_Patient);

    ASSERT_TRUE(backend_->IsExistingResource(id));
    ASSERT_FALSE(backend_->IsExistingResource(id + 1));
   
    backend_->DeleteResource(id);
    ASSERT_FALSE(backend_->IsExistingResource(id));
}

TEST_F (MongoDBBackendTest, ProtectedPatient) 
{
    int64_t pId = 1001;

    bool isProtected = backend_->IsProtectedPatient(pId);
    ASSERT_EQ(isProtected, false);

    backend_->SetProtectedPatient(pId, true);
    isProtected = backend_->IsProtectedPatient(pId);
    ASSERT_EQ(isProtected, true);

    backend_->SetProtectedPatient(pId, false);
    isProtected = backend_->IsProtectedPatient(pId);
    ASSERT_EQ(isProtected, false);
}
 
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
