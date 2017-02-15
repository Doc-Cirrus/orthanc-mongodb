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

#include <json/reader.h>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/uri.hpp>

std::string connection_str = "mongodb://localhost:27017/";
std::string test_database = "test_db_" + OrthancPlugins::GenerateUuid();

static int DatabaseAnswerCount = 0;
static int DatabaseVersion = GlobalProperty_DatabaseSchemaVersion;

OrthancPluginErrorCode PluginServiceMock(struct _OrthancPluginContext_t* context,
                                              _OrthancPluginService service,
                                              const void* params)
{
    if (service == _OrthancPluginService_GetExpectedDatabaseVersion) {
        _OrthancPluginReturnSingleValue *p = static_cast<_OrthancPluginReturnSingleValue *>(
                                                    const_cast<void *>(params));
        (*p->resultUint32) = DatabaseVersion;
    }
    else if (service == _OrthancPluginService_DatabaseAnswer)
    {
        DatabaseAnswerCount++;
    }
    else
    {
        //std::cout << "Unknown type: " << service << std::endl;
    }
    return OrthancPluginErrorCode_Success;
}




class MongoDBBackendTest : public ::testing::Test {
 protected:

    std::unique_ptr<OrthancPluginContext> context_;
    std::unique_ptr<OrthancPlugins::MongoDBBackend> backend_;
    std::unique_ptr<OrthancPlugins::DatabaseBackendOutput> output_; // IDatabaseBackend delete the registered output in the desctructor.

  virtual void SetUp() {
    std::unique_ptr<OrthancPlugins::MongoDBConnection> connection = std::make_unique<OrthancPlugins::MongoDBConnection>();
    connection->SetConnectionUri(std::string(connection_str) + test_database);
    context_ = std::make_unique<OrthancPluginContext>();
    context_->InvokeService = &PluginServiceMock;
    output_ = std::make_unique<OrthancPlugins::DatabaseBackendOutput>(context_.get(), static_cast<OrthancPluginDatabaseContext*>(NULL));

    backend_ = std::make_unique<OrthancPlugins::MongoDBBackend>(context_.get(), connection.release());
    backend_->RegisterOutput(output_.release());

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

    std::list<std::string> list3;
    backend_->GetAllPublicIds(list3, OrthancPluginResourceType_Patient, 0, 1);
    ASSERT_EQ(1, list3.size());
    ASSERT_EQ("", *list3.begin());

    uint64_t count = backend_->GetResourceCount(OrthancPluginResourceType_Patient);
    ASSERT_EQ(count, 1);

    OrthancPluginResourceType rt = backend_->GetResourceType(id);
    ASSERT_EQ(rt, OrthancPluginResourceType_Patient);

    ASSERT_TRUE(backend_->IsExistingResource(id));
    ASSERT_FALSE(backend_->IsExistingResource(id + 1));

    //Create structure for the resource
    backend_->AddAttachment(id, attachment);
    int64_t childId = backend_->CreateResource("", OrthancPluginResourceType_Series);
    backend_->AttachChild(id, childId);
    backend_->AddAttachment(childId, attachment);

    std::list<int64_t> list4;
    backend_->GetChildrenInternalId(list4, id);
    ASSERT_EQ(1, list4.size());
    ASSERT_EQ(childId, *list4.begin());

    std::list<std::string> list5;
    backend_->GetChildrenPublicId(list5, id);
    ASSERT_EQ(1, list.size());
    ASSERT_EQ("", *list1.begin());

    int64_t parentId;
    bool parent = backend_->LookupParent(parentId, childId);
    ASSERT_TRUE(parent);
    ASSERT_EQ(parentId, id);
   
    backend_->DeleteResource(id);
    ASSERT_FALSE(backend_->IsExistingResource(id));
    ASSERT_FALSE(backend_->IsExistingResource(childId));
    ASSERT_FALSE(backend_->LookupAttachment(id, 0));
    ASSERT_FALSE(backend_->LookupAttachment(childId, 0));
}

OrthancPluginChange change = {
    0, //seq;
    0, //changeType;
    OrthancPluginResourceType_Patient, //resourceType;
    "publicId", //publicId;
    "date" //date;
};

TEST_F (MongoDBBackendTest, Changes)
{
    int64_t id = backend_->CreateResource(change.publicId, OrthancPluginResourceType_Patient);
    
    for (int i = 0; i < 10; i++)
    {
        backend_->LogChange(change);
    }

    bool done = false;
    int count = 0;
    while (!done)
    {
        backend_->GetChanges(done, count, 1);
        if (!done)
        {
            count++;
        }
    }
    ASSERT_EQ(count, 9);

    DatabaseAnswerCount = 0;
    backend_->GetLastChange();
    ASSERT_EQ(DatabaseAnswerCount, 1);
        
    backend_->ClearChanges();
}

OrthancPluginExportedResource exportedResource =
{
    0, //seq;
    OrthancPluginResourceType_Patient, //resourceType;
    "publicId", //publicId;
    "modality", //modality;
    "date", //date;
    "patientId", //patientId;
    "studyInstanceUid", //studyInstanceUid;
    "seriesInstanceUid", //seriesInstanceUid;
    "sopInstanceUid", //sopInstanceUid;
};

TEST_F (MongoDBBackendTest, ExportedResources)
{
    int64_t id = backend_->CreateResource(change.publicId, OrthancPluginResourceType_Patient);
    
    for (int i = 0; i < 10; i++)
    {
        backend_->LogExportedResource(exportedResource);
    }

    bool done = false;
    int count = 0;
    while (!done)
    {
        backend_->GetExportedResources(done, count, 1);
        if (!done)
        {
            count++;
        }
    }
    ASSERT_EQ(count, 9);

    DatabaseAnswerCount = 0;
    backend_->GetLastExportedResource();
    ASSERT_EQ(DatabaseAnswerCount, 1);
        
    backend_->ClearExportedResources();
}

TEST_F (MongoDBBackendTest, Metadata)
{
    int64_t id = backend_->CreateResource(change.publicId, OrthancPluginResourceType_Patient);

    backend_->SetMetadata(id, 0, "meta");

    std::list<int32_t>list;
    backend_->ListAvailableMetadata(list, id);
    ASSERT_EQ(list.size(), 1);
    ASSERT_EQ(*list.begin(), 0);

    std::string res;
    ASSERT_TRUE(backend_->LookupMetadata(res, id, 0));
    ASSERT_EQ(res, "meta");

    backend_->DeleteMetadata(id, 0);

    ASSERT_FALSE(backend_->LookupMetadata(res, id, 0));
}

TEST_F(MongoDBBackendTest, GlobalProperty)
{
    backend_->SetGlobalProperty(0, "property");
    std::string p;
    backend_->LookupGlobalProperty(p, 0);
    ASSERT_EQ(p, "property");
}

TEST_F (MongoDBBackendTest, ProtectedPatient) 
{
    int64_t id1 = backend_->CreateResource("", OrthancPluginResourceType_Patient);
    int64_t id2 = backend_->CreateResource("", OrthancPluginResourceType_Patient);

    bool isProtected = backend_->IsProtectedPatient(id1);
    ASSERT_EQ(isProtected, false);

    backend_->SetProtectedPatient(id1, true);
    isProtected = backend_->IsProtectedPatient(id1);
    ASSERT_EQ(isProtected, true);

    backend_->SetProtectedPatient(id1, false);
    isProtected = backend_->IsProtectedPatient(id1);
    ASSERT_EQ(isProtected, false);

    int64_t rId;
    ASSERT_FALSE(backend_->SelectPatientToRecycle(rId));
    backend_->SetProtectedPatient(id1, true);
    backend_->SetProtectedPatient(id2, true);

    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(rId, id1);

    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId, id1));
    ASSERT_EQ(rId, id2);
}

TEST_F (MongoDBBackendTest, MainDicomTags) 
{
    int64_t parentId = backend_->CreateResource("", OrthancPluginResourceType_Patient);

    int64_t id1 = backend_->CreateResource("", OrthancPluginResourceType_Patient);
    int64_t id2 = backend_->CreateResource("", OrthancPluginResourceType_Patient);
    int64_t id3 = backend_->CreateResource("", OrthancPluginResourceType_Patient);
    int64_t id4 = backend_->CreateResource("", OrthancPluginResourceType_Patient);

    backend_->AttachChild(parentId, id1);
    backend_->AttachChild(parentId, id2);
    backend_->AttachChild(parentId, id3);
    backend_->AttachChild(parentId, id4);

    backend_->SetMainDicomTag(parentId, 0, 0, "");

    backend_->SetIdentifierTag(id1, 0, 0, "1");
    backend_->SetIdentifierTag(id2, 0, 0, "2");
    backend_->SetIdentifierTag(id3, 0, 0, "aaBBcc");
    backend_->SetIdentifierTag(id4, 0, 0, "");

    std::list<int64_t> list;
    backend_->LookupIdentifier(list, OrthancPluginResourceType_Patient,
    0, 0, OrthancPluginIdentifierConstraint_Equal, "");
    ASSERT_EQ(list.size(), 1);
    ASSERT_EQ(*list.begin(), id4);

    list.clear();
    backend_->LookupIdentifier(list, OrthancPluginResourceType_Patient,
    0, 0, OrthancPluginIdentifierConstraint_SmallerOrEqual, "1");
    ASSERT_EQ(list.size(), 2);
    ASSERT_EQ(*list.begin(), id1);

    list.clear();
    backend_->LookupIdentifier(list, OrthancPluginResourceType_Patient,
    0, 0, OrthancPluginIdentifierConstraint_GreaterOrEqual, "2");
    ASSERT_EQ(list.size(), 2);
    ASSERT_EQ(*list.begin(), id2);

    list.clear();
    backend_->LookupIdentifier(list, OrthancPluginResourceType_Patient,
    0, 0, OrthancPluginIdentifierConstraint_Wildcard, "*");
    ASSERT_EQ(list.size(), 4);

    list.clear();
    backend_->LookupIdentifier(list, OrthancPluginResourceType_Patient,
    0, 0, OrthancPluginIdentifierConstraint_Wildcard, "aa*");
    ASSERT_EQ(list.size(), 1);
    ASSERT_EQ(*list.begin(), id3);

    DatabaseAnswerCount = 0;
    backend_->GetMainDicomTags(parentId);
    ASSERT_EQ(DatabaseAnswerCount, 1);

    backend_->ClearMainDicomTags(0);
    backend_->DeleteResource(parentId); // delete resources
}

class ConfigurationTest : public ::testing::Test {
 protected:

    std::unique_ptr<OrthancPluginContext> context_;

  virtual void SetUp() {
    context_ = std::make_unique<OrthancPluginContext>();
    context_->InvokeService = &PluginServiceMock;
  }

  // virtual void TearDown() {}

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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
