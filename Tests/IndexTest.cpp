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

constexpr auto DEFAULT_DB_URI = "localhost:27017";

std::string connection_str = "mongodb://";
std::string test_database = "test_db_" + OrthancPlugins::GenerateUuid();

static int DatabaseAnswerCount = 0;
static int DatabaseVersion = GlobalProperty_DatabaseSchemaVersion;

OrthancPluginErrorCode PluginServiceMock(struct _OrthancPluginContext_t* context,
                                              _OrthancPluginService service,
                                              const void* params)
{
    if (service == _OrthancPluginService_GetExpectedDatabaseVersion)
    {
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

  virtual void SetUp()
  {
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

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
TEST_F (MongoDBBackendTest, LookupResourceAndParent)
{
    OrthancPluginResourceType type;
    std::string parentPublicId;
    bool lookup;

    const char *testParentPublicId = "testParentPublicId";
    const char *testChildPublicId = "testChildPublicId";

    int64_t id = 0;
    int64_t parentId = backend_->CreateResource(testParentPublicId, OrthancPluginResourceType_Series);
    int64_t childId = backend_->CreateResource(testChildPublicId, OrthancPluginResourceType_Patient);

    backend_->AttachChild(parentId, childId);

    lookup = backend_->LookupResourceAndParent(id, type, parentPublicId, testChildPublicId);
    ASSERT_TRUE(lookup);
    ASSERT_EQ(childId, id);
    ASSERT_EQ(testParentPublicId, parentPublicId);
    ASSERT_EQ(OrthancPluginResourceType_Patient, type);

    lookup = backend_->LookupResourceAndParent(id, type, parentPublicId, testParentPublicId);
    ASSERT_TRUE(lookup);
    ASSERT_EQ(parentId, id);
    ASSERT_EQ("", parentPublicId);
    ASSERT_EQ(OrthancPluginResourceType_Series, type);

    lookup = backend_->LookupResourceAndParent(id, type, parentPublicId, "");
    ASSERT_FALSE(lookup);
}
#  endif
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
TEST_F (MongoDBBackendTest, LookupResources)
{
    std::vector<int64_t> resources =
    {
        backend_->CreateResource("a", OrthancPluginResourceType_Study),
        backend_->CreateResource("b", OrthancPluginResourceType_Study),
        backend_->CreateResource("c", OrthancPluginResourceType_Study),
        backend_->CreateResource("d", OrthancPluginResourceType_Series),
        backend_->CreateResource("e", OrthancPluginResourceType_Series),
        backend_->CreateResource("f", OrthancPluginResourceType_Series),
        backend_->CreateResource("g", OrthancPluginResourceType_Instance),
    };

    backend_->SetIdentifierTag(resources[0], 0, 1, "tag");
    backend_->SetIdentifierTag(resources[1], 0, 1, "tag");
    backend_->SetIdentifierTag(resources[2], 0, 1, "tag");
    backend_->SetMainDicomTag(resources[3], 0, 1, "tag");
    backend_->SetMainDicomTag(resources[4], 0, 1, "tag");
    backend_->SetMainDicomTag(resources[5], 1, 0, "tag");
    backend_->SetMainDicomTag(resources[6], 1, 1, "tag");

    DatabaseAnswerCount = 0;

    const char *pattern[] = {"tag", ""};

    std::vector<OrthancPluginDatabaseConstraint> constraints
    {
        {
            .level = OrthancPluginResourceType_Study,
            .tagGroup = 0,
            .tagElement = 1,
            .isIdentifierTag = 1,
            .isCaseSensitive = 1,
            .isMandatory = 0,
            .type = OrthancPluginConstraintType_Equal,
            .valuesCount = 1,
            .values = pattern
        }
    };

    backend_->LookupResources(constraints, OrthancPluginResourceType_Study, resources.size(), 0);
    ASSERT_EQ(3, DatabaseAnswerCount);

    DatabaseAnswerCount = 0;

    constraints[0].type = OrthancPluginConstraintType_SmallerOrEqual;
    constraints[0].isCaseSensitive = 0;
    /**
        .level = OrthancPluginResourceType_Study,
        .tagGroup = 0,
        .tagElement = 1,
        .isIdentifierTag = 1,
        .isCaseSensitive = 0,
        .isMandatory = 0,
        .type = OrthancPluginConstraintType_SmallerOrEqual,
        .valuesCount = 1,
        .values = pattern
     **/
    backend_->LookupResources(constraints, OrthancPluginResourceType_Study, resources.size(), 0);
    ASSERT_EQ(3, DatabaseAnswerCount);

    DatabaseAnswerCount = 0;

    constraints[0].type = OrthancPluginConstraintType_GreaterOrEqual;
    constraints[0].level = OrthancPluginResourceType_Series;
    constraints[0].isIdentifierTag = 0;
    /**
        .level = OrthancPluginResourceType_Series,
        .tagGroup = 0,
        .tagElement = 1,
        .isIdentifierTag = 0,
        .isCaseSensitive = 0,
        .isMandatory = 0,
        .type = OrthancPluginConstraintType_GreaterOrEqual,
        .valuesCount = 1,
        .values = pattern
     **/
    backend_->LookupResources(constraints, OrthancPluginResourceType_Series, resources.size(), 0);
    ASSERT_EQ(2, DatabaseAnswerCount);

    DatabaseAnswerCount = 0;

    constraints[0].type = OrthancPluginConstraintType_Wildcard;
    /**
        .level = OrthancPluginResourceType_Series,
        .tagGroup = 0,
        .tagElement = 1,
        .isIdentifierTag = 0,
        .isCaseSensitive = 0,
        .isMandatory = 0,
        .type = OrthancPluginConstraintType_Wildcard,
        .valuesCount = 1,
        .values = pattern
     **/
    backend_->LookupResources(constraints, OrthancPluginResourceType_Series, resources.size(), 0);
    ASSERT_EQ(2, DatabaseAnswerCount);

    DatabaseAnswerCount = 0;

    constraints[0].type = OrthancPluginConstraintType_List;
    constraints[0].valuesCount = 2;
    /**
        .level = OrthancPluginResourceType_Series,
        .tagGroup = 0,
        .tagElement = 1,
        .isIdentifierTag = 0,
        .isCaseSensitive = 0,
        .isMandatory = 0,
        .type = OrthancPluginConstraintType_List,
        .valuesCount = 2,
        .values = pattern
     **/
    backend_->LookupResources(constraints, OrthancPluginResourceType_Series, resources.size(), 0);
    ASSERT_EQ(2, DatabaseAnswerCount);

    DatabaseAnswerCount = 0;

    constraints[0].tagGroup = 1;
    constraints[0].tagElement = 0;
    constraints[0].valuesCount = 1;
    /**
        .level = OrthancPluginResourceType_Series,
        .tagGroup = 1,
        .tagElement = 0,
        .isIdentifierTag = 0,
        .isCaseSensitive = 0,
        .isMandatory = 0,
        .type = OrthancPluginConstraintType_List,
        .valuesCount = 1,
        .values = pattern
     **/
    backend_->LookupResources(constraints, OrthancPluginResourceType_Series, resources.size(), 0);
    ASSERT_EQ(1, DatabaseAnswerCount);

    DatabaseAnswerCount = 0;

    constraints[0].tagElement = 1;
    /**
        .level = OrthancPluginResourceType_Series,
        .tagGroup = 1,
        .tagElement = 1,
        .isIdentifierTag = 0,
        .isCaseSensitive = 0,
        .isMandatory = 0,
        .type = OrthancPluginConstraintType_List,
        .valuesCount = 1,
        .values = pattern
     **/
    backend_->AttachChild(resources[6], backend_->CreateResource("", OrthancPluginResourceType_Instance));
    backend_->LookupResources(constraints, OrthancPluginResourceType_Instance, resources.size(), 1);
    ASSERT_EQ(1, DatabaseAnswerCount);
}
#endif

OrthancPluginChange change = {
    0, //seq;
    0, //changeType;
    OrthancPluginResourceType_Patient, //resourceType;
    "publicId", //publicId;
    "date" //date;
};

TEST_F (MongoDBBackendTest, Changes)
{
    int changesCounter = 10;
    int64_t id = backend_->CreateResource(change.publicId, OrthancPluginResourceType_Patient);

    ASSERT_EQ(0, backend_->GetLastChangeIndex());

    for (int i = 0; i < changesCounter; i++)
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
    ASSERT_EQ(changesCounter, backend_->GetLastChangeIndex());

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

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)
TEST_F (MongoDBBackendTest, AllMetadata)
{
    std::map<int32_t, std::string> md;

    int64_t id = backend_->CreateResource(change.publicId, OrthancPluginResourceType_Patient);

    backend_->GetAllMetadata(md, id);
    ASSERT_EQ(0u, md.size());

    backend_->SetMetadata(id, 3, "PINNACLE");
    backend_->GetAllMetadata(md, id);
    ASSERT_EQ(1u, md.size());
    ASSERT_EQ("PINNACLE", md[3]);
    backend_->SetMetadata(id, 5, "TUTU");
    backend_->GetAllMetadata(md, id);
    ASSERT_EQ(2u, md.size());

    std::map<int32_t, std::string> md2;
    backend_->GetAllMetadata(md2, id);
    ASSERT_EQ(2u, md2.size());
    ASSERT_EQ("TUTU", md2[5]);
    ASSERT_EQ("PINNACLE", md2[3]);

    md.clear();
    md2.clear();

    backend_->DeleteMetadata(id, 5);
    backend_->GetAllMetadata(md, id);
    ASSERT_EQ(1u, md.size());
    ASSERT_EQ("PINNACLE", md[3]);

    backend_->GetAllMetadata(md2, id);
    ASSERT_EQ(1u, md2.size());
    ASSERT_EQ("PINNACLE", md2[3]);

    backend_->DeleteMetadata(id, 3);

    md.clear();

    backend_->GetAllMetadata(md, id);
    ASSERT_EQ(0u, md.size());
}
#  endif
#endif

TEST_F (MongoDBBackendTest, ChildrenMetadata)
{
    std::list<std::string> values;

    int64_t parentId = backend_->CreateResource("", OrthancPluginResourceType_Patient);

    int64_t id1 = backend_->CreateResource("", OrthancPluginResourceType_Patient);
    int64_t id2 = backend_->CreateResource("", OrthancPluginResourceType_Patient);

    backend_->AttachChild(parentId, id1);
    backend_->AttachChild(parentId, id2);

    backend_->GetChildrenMetadata(values, parentId, 0);
    ASSERT_EQ(values.size(), 0);

    backend_->SetMetadata(id1, 0, "meta");
    backend_->SetMetadata(id2, 0, "meta");

    backend_->GetChildrenMetadata(values, parentId, 0);
    ASSERT_EQ(values.size(), 2);

    backend_->DeleteMetadata(id1, 0);
    backend_->DeleteMetadata(id2, 0);

    values.clear();

    backend_->GetChildrenMetadata(values, parentId, 0);
    ASSERT_EQ(values.size(), 0);
}

TEST_F(MongoDBBackendTest, GlobalProperty)
{
    backend_->SetGlobalProperty(0, "property");
    std::string p;
    ASSERT_TRUE(backend_->LookupGlobalProperty(p, 0));
    ASSERT_EQ(p, "property");

    p.clear();

    ASSERT_FALSE(backend_->LookupGlobalProperty(p, 1));
    ASSERT_TRUE(p.empty());
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
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(rId, id2); // now patient with id1 is the newest in PatientRecyclingOrder

    backend_->SetProtectedPatient(id1, true);
    backend_->SetProtectedPatient(id2, true);

    rId = 0;
    ASSERT_FALSE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(0, rId);

    backend_->SetProtectedPatient(id1, false);
    backend_->SetProtectedPatient(id2, false);

    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(rId, id1);

    rId = 0;
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId, id1));
    ASSERT_EQ(rId, id2);

    rId = 0;

    backend_->SetProtectedPatient(id1, true);
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(rId, id2);

    rId = 0;

    backend_->SetProtectedPatient(id2, true);
    ASSERT_FALSE(backend_->SelectPatientToRecycle(rId, id1));
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

TEST_F (MongoDBBackendTest, LookupRange)
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
    backend_->SetIdentifierTag(id3, 0, 0, "3");
    backend_->SetIdentifierTag(id4, 0, 0, "4");

    std::list<int64_t> list;
    backend_->LookupIdentifierRange(list, OrthancPluginResourceType_Patient,
    0, 0, "2", "3");
    ASSERT_EQ(list.size(), 2);
    ASSERT_EQ(*list.begin(), id2);


    backend_->ClearMainDicomTags(0);
    backend_->DeleteResource(parentId); // delete resources
}

TEST_F (MongoDBBackendTest, TagMostRecentPatient)
{
    std::vector<int64_t> patients =
    {
        backend_->CreateResource("hash1", OrthancPluginResourceType_Patient),
        backend_->CreateResource("hash2", OrthancPluginResourceType_Patient),
        backend_->CreateResource("hash3", OrthancPluginResourceType_Patient),
    };

    int64_t rId;
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(patients[0], rId);

    rId = 0;

    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId, patients[0]));
    ASSERT_EQ(patients[1], rId);

    rId = 0;

    // Tag patients[0] as the most recent
    backend_->TagMostRecentPatient(patients[0]);
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(patients[1], rId);

    rId = 0;

    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId, patients[1]));
    ASSERT_EQ(patients[2], rId);

    rId = 0;

    // Tag patients[1] as the most recent
    backend_->TagMostRecentPatient(patients[1]);
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(patients[2], rId);

    // Delete patients[0]
    backend_->DeleteResource(patients[0]);

    rId = 0;

    // Tag patients[0] as the most recent - no effect
    backend_->TagMostRecentPatient(patients[0]);
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(patients[2], rId);

    rId = 0;

    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId, patients[2]));
    ASSERT_EQ(patients[1], rId);

    // Delete patients[2]
    backend_->DeleteResource(patients[2]);

    rId = 0;

    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(patients[1], rId);

    // Delete patients[1]
    backend_->DeleteResource(patients[1]);

    rId = 0;

    ASSERT_FALSE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(0, rId);
}

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
TEST_F (MongoDBBackendTest, CreateInstance)
{
    ASSERT_TRUE(backend_->HasCreateInstance());

    std::string patientHash, studyHash, seriesHash, instanceHash;

    // Store hash and id for each resource type of first created instance
    std::vector<std::pair<std::string, int64_t>> instanceHashes =
    {
        // Patient hash
        std::make_pair(OrthancPlugins::GenerateUuid(), 0),
        // Study hash
        std::make_pair(OrthancPlugins::GenerateUuid(), 0),
        // Series hash
        std::make_pair(OrthancPlugins::GenerateUuid(), 0),
        // Instance hash
        std::make_pair(OrthancPlugins::GenerateUuid(), 0)
    };

    OrthancPluginCreateInstanceResult output;
    memset(&output, 0, sizeof(output));

    backend_->CreateInstance(output,
                            instanceHashes[0].first.c_str(),
                            instanceHashes[1].first.c_str(),
                            instanceHashes[2].first.c_str(),
                            instanceHashes[3].first.c_str());

    ASSERT_TRUE(output.isNewPatient);
    ASSERT_NE(0, output.patientId);
    instanceHashes[0].second = output.patientId;

    ASSERT_TRUE(output.isNewStudy);
    ASSERT_NE(0, output.studyId);
    instanceHashes[1].second = output.studyId;

    ASSERT_TRUE(output.isNewSeries);
    ASSERT_NE(0, output.seriesId);
    instanceHashes[2].second = output.seriesId;

    ASSERT_TRUE(output.isNewInstance);
    ASSERT_NE(0, output.instanceId);
    instanceHashes[3].second = output.instanceId;

    memset(&output, 0, sizeof(output));

    // Create instance with existing hash
    backend_->CreateInstance(output,
                            "",
                            "",
                            "",
                            instanceHashes[3].first.c_str());

    ASSERT_FALSE(output.isNewInstance);
    ASSERT_EQ(instanceHashes[3].second, output.instanceId);

    memset(&output, 0, sizeof(output));

    // Create new instance with existing series hash
    patientHash = OrthancPlugins::GenerateUuid();
    studyHash = OrthancPlugins::GenerateUuid();
    instanceHash = OrthancPlugins::GenerateUuid();
    backend_->CreateInstance(output,
                            patientHash.c_str(),
                            studyHash.c_str(),
                            instanceHashes[2].first.c_str(),
                            instanceHash.c_str());

    ASSERT_TRUE(output.isNewPatient);
    ASSERT_TRUE(output.isNewStudy);
    ASSERT_FALSE(output.isNewSeries);
    ASSERT_EQ(instanceHashes[2].second, output.seriesId);
    ASSERT_TRUE(output.isNewInstance);

    memset(&output, 0, sizeof(output));

    // Create new instance with existing study hash
    patientHash = OrthancPlugins::GenerateUuid();
    seriesHash = OrthancPlugins::GenerateUuid();
    instanceHash = OrthancPlugins::GenerateUuid();
    backend_->CreateInstance(output,
                            patientHash.c_str(),
                            instanceHashes[1].first.c_str(),
                            seriesHash.c_str(),
                            instanceHash.c_str());

    ASSERT_TRUE(output.isNewPatient);
    ASSERT_FALSE(output.isNewStudy);
    ASSERT_EQ(instanceHashes[1].second, output.studyId);
    ASSERT_TRUE(output.isNewSeries);
    ASSERT_TRUE(output.isNewInstance);

    memset(&output, 0, sizeof(output));

    int64_t rId;
    // Check that the first created patient is the first one in PatientRecyclingOrder
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_EQ(instanceHashes[0].second, rId);

    // Create new instance with existing patient hash
    studyHash = OrthancPlugins::GenerateUuid();
    seriesHash = OrthancPlugins::GenerateUuid();
    instanceHash = OrthancPlugins::GenerateUuid();
    backend_->CreateInstance(output,
                            instanceHashes[0].first.c_str(),
                            studyHash.c_str(),
                            seriesHash.c_str(),
                            instanceHash.c_str());

    ASSERT_FALSE(output.isNewPatient);
    ASSERT_EQ(instanceHashes[0].second, output.patientId);
    ASSERT_TRUE(output.isNewStudy);
    ASSERT_TRUE(output.isNewSeries);
    ASSERT_TRUE(output.isNewInstance);

    rId = 0;
    // Check that now the first created patient is not the first one in PatientRecyclingOrder
    ASSERT_TRUE(backend_->SelectPatientToRecycle(rId));
    ASSERT_NE(instanceHashes[0].second, rId);
}
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
TEST_F (MongoDBBackendTest, SetResourcesContent)
{
    std::string metadataVal1 = OrthancPlugins::GenerateUuid();
    std::string metadataVal2 = OrthancPlugins::GenerateUuid();

    std::vector<OrthancPluginResourcesContentTags> identifierTags =
    {
        {
            .resource = 1,
            .group = 0,
            .element = 0,
            .value = metadataVal1.c_str()
        },
        {
            .resource = 2,
            .group = 0,
            .element = 0,
            .value = metadataVal2.c_str()
        },
    };
    std::vector<OrthancPluginResourcesContentTags> mainDicomTags =
    {
        {
            .resource = 1,
            .group = 1,
            .element = 0,
            .value = metadataVal1.c_str()
        },
        {
            .resource = 1,
            .group = 1,
            .element = 0,
            .value = metadataVal2.c_str()
        },
    };
    std::vector<OrthancPluginResourcesContentMetadata> metadata =
    {
        {
            .resource = 1,
            .metadata = 0,
            .value = metadataVal1.c_str()
        },
        {
            .resource = 2,
            .metadata = 0,
            .value = metadataVal2.c_str()
        },
    };

    backend_->SetResourcesContent(identifierTags.size(), &identifierTags[0],
                                  mainDicomTags.size(), &mainDicomTags[0],
                                  metadata.size(), &metadata[0]);

    std::list<int64_t> list;
    OrthancPluginResourceType dummy;

    // Test identifier tags
    backend_->LookupIdentifier(list, dummy, 0, 0, OrthancPluginIdentifierConstraint_Equal, metadataVal1.c_str());
    ASSERT_EQ(1, list.size());
    ASSERT_EQ(1, list.front());

    list.clear();

    backend_->LookupIdentifier(list, dummy, 0, 0, OrthancPluginIdentifierConstraint_Equal, metadataVal2.c_str());
    ASSERT_EQ(1, list.size());
    ASSERT_EQ(2, list.front());

    list.clear();
    DatabaseAnswerCount = 0;

    // Test main dicom tags
    backend_->GetMainDicomTags(1);
    ASSERT_EQ(DatabaseAnswerCount, 2);

    DatabaseAnswerCount = 0;

    std::string res;

    // Test metadata
    ASSERT_TRUE(backend_->LookupMetadata(res, 1, 0));
    ASSERT_EQ(metadataVal1, res);

    ASSERT_TRUE(backend_->LookupMetadata(res, 2, 0));
    ASSERT_EQ(metadataVal2, res);
}
#endif

int main(int argc, char **argv)
{
  if ( argc > 1 )
  {
    connection_str.append(argv[1]);
  }
  else
  {
    connection_str.append(DEFAULT_DB_URI);
  }

  connection_str.append("/");

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
