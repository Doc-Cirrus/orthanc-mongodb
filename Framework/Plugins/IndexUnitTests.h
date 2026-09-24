/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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


#pragma once

#include "IndexBackend.h"
#include "GlobalProperties.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <SystemToolbox.h>

#include <gtest/gtest.h>
#include <list>

#if !defined(ORTHANC_DATABASE_VERSION)
// This happens if using the Orthanc framework system-wide library
#  define ORTHANC_DATABASE_VERSION 6
#endif


#if ORTHANC_ENABLE_POSTGRESQL == 1
#  define HAS_REVISIONS 1
// we can not test patient protection in PG because it is now deeply intricated in the CreateInstance function that is too difficult to call from here
#  define CAN_TEST_PATIENT_PROTECTION 0
#elif ORTHANC_ENABLE_MYSQL == 1
#  define HAS_REVISIONS 0
#  define CAN_TEST_PATIENT_PROTECTION 1
#elif ORTHANC_ENABLE_SQLITE == 1
#  define HAS_REVISIONS 1
#  define CAN_TEST_PATIENT_PROTECTION 1
#elif ORTHANC_ENABLE_ODBC == 1
#  define HAS_REVISIONS 1
#  define CAN_TEST_PATIENT_PROTECTION 1
#elif ORTHANC_ENABLE_MONGODB == 1
#  define HAS_REVISIONS 1
#  define CAN_TEST_PATIENT_PROTECTION 1
// Values over 15MB are stored in GridFS (cf. "LargeGlobalProperty")
#  define CAN_TEST_LARGE_PROPERTIES 1
// The cascade is implemented in "MongoDBResources::DeleteResource()"
#  define CAN_TEST_CASCADE_DELETE 1
#  include "../../MongoDB/UnitTests/MongoDBTestsToolbox.h"
#else
#  error Unknown database backend
#endif

#if !defined(CAN_TEST_LARGE_PROPERTIES)
#  define CAN_TEST_LARGE_PROPERTIES 1
#endif

#if !defined(CAN_TEST_CASCADE_DELETE)
#  define CAN_TEST_CASCADE_DELETE 1
#endif


namespace Orthanc
{
  /**
   * Mock enumeration inspired from the source code of Orthanc... only
   * for use in the unit tests!
   * https://orthanc.uclouvain.be/hg/orthanc/file/default/OrthancServer/Sources/ServerEnumerations.h
   **/
  enum MetadataType
  {
    MetadataType_ModifiedFrom,
    MetadataType_LastUpdate
  };
}


/**
 * This is a sample UTF8 string that is the concatenation of a Korean
 * and a Kanji text. Check out "utf8raw" in
 * "OrthancFramework/UnitTestsSources/FromDcmtkTests.cpp" for the
 * sources of these binary values.
 **/
static const uint8_t UTF8[] = {
  // cf. TEST(Toolbox, EncodingsKorean)
  0x48, 0x6f, 0x6e, 0x67, 0x5e, 0x47, 0x69, 0x6c, 0x64, 0x6f, 0x6e, 0x67, 0x3d, 0xe6,
  0xb4, 0xaa, 0x5e, 0xe5, 0x90, 0x89, 0xe6, 0xb4, 0x9e, 0x3d, 0xed, 0x99, 0x8d, 0x5e,
  0xea, 0xb8, 0xb8, 0xeb, 0x8f, 0x99,
  
  // cf. TEST(Toolbox, EncodingsJapaneseKanji)
  0x59, 0x61, 0x6d, 0x61, 0x64, 0x61, 0x5e, 0x54, 0x61, 0x72, 0x6f, 0x75, 0x3d, 0xe5,
  0xb1, 0xb1, 0xe7, 0x94, 0xb0, 0x5e, 0xe5, 0xa4, 0xaa, 0xe9, 0x83, 0x8e, 0x3d, 0xe3,
  0x82, 0x84, 0xe3, 0x81, 0xbe, 0xe3, 0x81, 0xa0, 0x5e, 0xe3, 0x81, 0x9f, 0xe3, 0x82,
  0x8d, 0xe3, 0x81, 0x86,

  // End of text
  0x00
};


static std::unique_ptr<OrthancPluginAttachment>  expectedAttachment;
static std::list<OrthancPluginDicomTag>  expectedDicomTags;
static std::unique_ptr<OrthancPluginExportedResource>  expectedExported;

static std::map<std::string, OrthancPluginResourceType> deletedResources;
static std::unique_ptr< std::pair<std::string, OrthancPluginResourceType> > remainingAncestor;
static std::set<std::string> deletedAttachments;
static unsigned int countDicomTags = 0;


static void CheckAttachment(const OrthancPluginAttachment& attachment)
{
  ASSERT_STREQ(expectedAttachment->uuid, attachment.uuid);
  ASSERT_EQ(expectedAttachment->contentType, attachment.contentType);
  ASSERT_EQ(expectedAttachment->uncompressedSize, attachment.uncompressedSize);
  ASSERT_STREQ(expectedAttachment->uncompressedHash, attachment.uncompressedHash);
  ASSERT_EQ(expectedAttachment->compressionType, attachment.compressionType);
  ASSERT_EQ(expectedAttachment->compressedSize, attachment.compressedSize);
  ASSERT_STREQ(expectedAttachment->compressedHash, attachment.compressedHash);
}

static void CheckExportedResource(const OrthancPluginExportedResource& exported)
{
  // ASSERT_EQ(expectedExported->seq, exported.seq);
  ASSERT_EQ(expectedExported->resourceType, exported.resourceType);
  ASSERT_STREQ(expectedExported->publicId, exported.publicId);
  ASSERT_STREQ(expectedExported->modality, exported.modality);
  ASSERT_STREQ(expectedExported->date, exported.date);
  ASSERT_STREQ(expectedExported->patientId, exported.patientId);
  ASSERT_STREQ(expectedExported->studyInstanceUid, exported.studyInstanceUid);
  ASSERT_STREQ(expectedExported->seriesInstanceUid, exported.seriesInstanceUid);
  ASSERT_STREQ(expectedExported->sopInstanceUid, exported.sopInstanceUid);
}

static void CheckDicomTag(const OrthancPluginDicomTag& tag)
{
  for (std::list<OrthancPluginDicomTag>::const_iterator
         it = expectedDicomTags.begin(); it != expectedDicomTags.end(); ++it)
  {
    if (it->group == tag.group &&
        it->element == tag.element &&
        !strcmp(it->value, tag.value))
    {
      // OK, match
      return;
    }
  }

  ASSERT_TRUE(0);  // Error
}



/**
 * Records the answers of the index backend into the global variables
 * above. Upstream, this is done by the output of
 * "DatabaseBackendAdapterV2", which is not compiled in this plugin
 * since it only supports the V4 database SDK.
 **/
class TestOutput : public OrthancDatabases::IDatabaseBackendOutput
{
public:
  class Factory : public IDatabaseBackendOutput::IFactory
  {
  public:
    virtual IDatabaseBackendOutput* CreateOutput() ORTHANC_OVERRIDE
    {
      return new TestOutput;
    }
  };

  virtual void SignalDeletedAttachment(const std::string& uuid,
                                       int32_t            contentType,
                                       uint64_t           uncompressedSize,
                                       const std::string& uncompressedHash,
                                       int32_t            compressionType,
                                       uint64_t           compressedSize,
                                       const std::string& compressedHash,
                                       const std::string& customData) ORTHANC_OVERRIDE
  {
    deletedAttachments.insert(uuid);
  }

  virtual void SignalDeletedResource(const std::string& publicId,
                                     OrthancPluginResourceType resourceType) ORTHANC_OVERRIDE
  {
    deletedResources[publicId] = resourceType;
  }

  virtual void SignalRemainingAncestor(const std::string& ancestorId,
                                       OrthancPluginResourceType ancestorType) ORTHANC_OVERRIDE
  {
    remainingAncestor.reset(new std::pair<std::string, OrthancPluginResourceType>());
    *remainingAncestor = std::make_pair(ancestorId, ancestorType);
  }

  virtual void AnswerAttachment(const std::string& uuid,
                                int32_t            contentType,
                                uint64_t           uncompressedSize,
                                const std::string& uncompressedHash,
                                int32_t            compressionType,
                                uint64_t           compressedSize,
                                const std::string& compressedHash,
                                const std::string& customData) ORTHANC_OVERRIDE
  {
    OrthancPluginAttachment attachment;
    attachment.uuid = uuid.c_str();
    attachment.contentType = contentType;
    attachment.uncompressedSize = uncompressedSize;
    attachment.uncompressedHash = uncompressedHash.c_str();
    attachment.compressionType = compressionType;
    attachment.compressedSize = compressedSize;
    attachment.compressedHash = compressedHash.c_str();
    CheckAttachment(attachment);
  }

  virtual void AnswerChange(int64_t                    seq,
                            int32_t                    changeType,
                            OrthancPluginResourceType  resourceType,
                            const std::string&         publicId,
                            const std::string&         date) ORTHANC_OVERRIDE
  {
  }

  virtual void AnswerDicomTag(uint16_t group,
                              uint16_t element,
                              const std::string& value) ORTHANC_OVERRIDE
  {
    OrthancPluginDicomTag tag;
    tag.group = group;
    tag.element = element;
    tag.value = value.c_str();
    CheckDicomTag(tag);
    countDicomTags++;
  }

  virtual void AnswerExportedResource(int64_t                    seq,
                                      OrthancPluginResourceType  resourceType,
                                      const std::string&         publicId,
                                      const std::string&         modality,
                                      const std::string&         date,
                                      const std::string&         patientId,
                                      const std::string&         studyInstanceUid,
                                      const std::string&         seriesInstanceUid,
                                      const std::string&         sopInstanceUid) ORTHANC_OVERRIDE
  {
    OrthancPluginExportedResource exported;
    exported.seq = seq;
    exported.resourceType = resourceType;
    exported.publicId = publicId.c_str();
    exported.modality = modality.c_str();
    exported.date = date.c_str();
    exported.patientId = patientId.c_str();
    exported.studyInstanceUid = studyInstanceUid.c_str();
    exported.seriesInstanceUid = seriesInstanceUid.c_str();
    exported.sopInstanceUid = sopInstanceUid.c_str();
    CheckExportedResource(exported);
  }

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  virtual void AnswerMatchingResource(const std::string& resourceId) ORTHANC_OVERRIDE
  {
  }

  virtual void AnswerMatchingResource(const std::string& resourceId,
                                      const std::string& someInstanceId) ORTHANC_OVERRIDE
  {
  }
#endif
};


static OrthancPluginErrorCode InvokeService(struct _OrthancPluginContext_t* context,
                                            _OrthancPluginService service,
                                            const void* params)
{
  switch (service)
  {
    case _OrthancPluginService_GetExpectedDatabaseVersion:
    {
      const _OrthancPluginReturnSingleValue& p =
        *reinterpret_cast<const _OrthancPluginReturnSingleValue*>(params);
      *(p.resultUint32) = ORTHANC_DATABASE_VERSION;
      return OrthancPluginErrorCode_Success;
    }

    default:
      assert(0);
      printf("Service not emulated: %d\n", service);
      return OrthancPluginErrorCode_NotImplemented;
  }
}


#if ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES == 1
static void ListKeys(std::set<std::string>& keys,
                     OrthancDatabases::IndexBackend& db,
                     OrthancDatabases::DatabaseManager& manager,
                     const std::string& storeId)
{
  {
    Orthanc::DatabasePluginMessages::ListKeysValues_Request request;
    request.set_store_id(storeId);
    request.set_from_first(true);
    request.set_limit(0);

    Orthanc::DatabasePluginMessages::TransactionResponse response;
    db.ListKeysValues(response, manager, request);

    keys.clear();

    for (int i = 0; i < response.list_keys_values().keys_values_size(); i++)
    {
      const Orthanc::DatabasePluginMessages::ListKeysValues_Response_KeyValue& item = response.list_keys_values().keys_values(i);
      keys.insert(item.key());

      std::string value;
      if (!db.GetKeyValue(value, manager, storeId, item.key()) ||
          value != item.value())
      {
        ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
      }
    }
  }

  {
    std::set<std::string> keys2;

    // Alternative implementation using an iterator
    Orthanc::DatabasePluginMessages::ListKeysValues_Request request;
    request.set_store_id(storeId);
    request.set_from_first(true);
    request.set_limit(1);

    Orthanc::DatabasePluginMessages::TransactionResponse response;
    db.ListKeysValues(response, manager, request);

    while (response.list_keys_values().keys_values_size() > 0)
    {
      int count = response.list_keys_values().keys_values_size();

      for (int i = 0; i < count; i++)
      {
        keys2.insert(response.list_keys_values().keys_values(i).key());
      }

      request.set_from_first(false);
      request.set_from_key(response.list_keys_values().keys_values(count - 1).key());
      db.ListKeysValues(response, manager, request);
    }

    if (keys.size() != keys2.size())
    {
      ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
    }
    else
    {
      for (std::set<std::string>::const_iterator it = keys.begin(); it != keys.end(); ++it)
      {
        if (keys2.find(*it) == keys2.end())
        {
          ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
        }
      }
    }
  }
}
#endif


static void FillBlob(std::string& blob)
{
  blob.clear();
  blob.push_back(0);
  blob.push_back(1);
  blob.push_back(0);
  blob.push_back(2);
}


static void CheckBlob(const std::string& s)
{
  ASSERT_EQ(4u, s.size());
  ASSERT_EQ(0u, static_cast<uint8_t>(s[0]));
  ASSERT_EQ(1u, static_cast<uint8_t>(s[1]));
  ASSERT_EQ(0u, static_cast<uint8_t>(s[2]));
  ASSERT_EQ(2u, static_cast<uint8_t>(s[3]));
}


TEST(IndexBackend, Basic)
{
  using namespace OrthancDatabases;

  OrthancPluginContext context;
  context.pluginsManager = NULL;
  context.orthancVersion = "mainline";
  context.Free = ::free;
  context.InvokeService = InvokeService;

#if ORTHANC_ENABLE_POSTGRESQL == 1
  PostgreSQLIndex db(&context, globalParameters_, false);
  db.SetClearAll(true);
#elif ORTHANC_ENABLE_MYSQL == 1
  MySQLIndex db(&context, globalParameters_, false);
  db.SetClearAll(true);
#elif ORTHANC_ENABLE_ODBC == 1
  OdbcIndex db(&context, connectionString_, false);
#elif ORTHANC_ENABLE_MONGODB == 1
  TestDatabase testDatabase;  // Dropped once "db" is destroyed
  MongoDBIndex db(&context, CreateTestParameters(testDatabase.GetUri()), false);
#elif ORTHANC_ENABLE_SQLITE == 1  // Must be the last one
  SQLiteIndex db(&context);  // Open in memory
#else
#  error Unsupported database backend
#endif

  db.SetOutputFactory(new TestOutput::Factory);

  std::list<IdentifierTag> identifierTags;
  std::unique_ptr<DatabaseManager> manager(IndexBackend::CreateSingleDatabaseManager(db, false, identifierTags));
  
  std::unique_ptr<IDatabaseBackendOutput> output(db.CreateOutput());

  {
    // Sanity check
    std::string blob;
    FillBlob(blob);
    CheckBlob(blob);
  }

  std::string a;
  ASSERT_TRUE(db.LookupGlobalProperty(a, *manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseSchemaVersion));
  ASSERT_EQ("6", a);

  db.SetGlobalProperty(*manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9, "Hello");
  ASSERT_TRUE(db.LookupGlobalProperty(a, *manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9));
  ASSERT_EQ("Hello", a);
  db.SetGlobalProperty(*manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9, "HelloWorld");
  ASSERT_TRUE(db.LookupGlobalProperty(a, *manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9));
  ASSERT_EQ("HelloWorld", a);

  ASSERT_EQ(0u, db.GetAllResourcesCount(*manager));
  ASSERT_EQ(0u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Patient));
  ASSERT_EQ(0u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Study));
  ASSERT_EQ(0u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Series));

  int64_t studyId = db.CreateResource(*manager, "study", OrthancPluginResourceType_Study);
  ASSERT_TRUE(db.IsExistingResource(*manager, studyId));
  ASSERT_FALSE(db.IsExistingResource(*manager, studyId + 1));

  int64_t tmp;
  OrthancPluginResourceType t;
  ASSERT_FALSE(db.LookupResource(tmp, t, *manager, "world"));
  ASSERT_TRUE(db.LookupResource(tmp, t, *manager, "study"));
  ASSERT_EQ(studyId, tmp);
  ASSERT_EQ(OrthancPluginResourceType_Study, t);
  
  int64_t seriesId = db.CreateResource(*manager, "series", OrthancPluginResourceType_Series);
  ASSERT_NE(studyId, seriesId);

  ASSERT_EQ("study", db.GetPublicId(*manager, studyId));
  ASSERT_EQ("series", db.GetPublicId(*manager, seriesId));
  ASSERT_EQ(OrthancPluginResourceType_Study, db.GetResourceType(*manager, studyId));
  ASSERT_EQ(OrthancPluginResourceType_Series, db.GetResourceType(*manager, seriesId));

  db.AttachChild(*manager, studyId, seriesId);

  ASSERT_FALSE(db.LookupParent(tmp, *manager, studyId));
  ASSERT_TRUE(db.LookupParent(tmp, *manager, seriesId));
  ASSERT_EQ(studyId, tmp);

  int64_t series2Id = db.CreateResource(*manager, "series2", OrthancPluginResourceType_Series);
  db.AttachChild(*manager, studyId, series2Id);

  ASSERT_EQ(3u, db.GetAllResourcesCount(*manager));
  ASSERT_EQ(0u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Patient));
  ASSERT_EQ(1u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Study));
  ASSERT_EQ(2u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Series));

  ASSERT_FALSE(db.GetParentPublicId(a, *manager, studyId));
  ASSERT_TRUE(db.GetParentPublicId(a, *manager, seriesId));  ASSERT_EQ("study", a);
  ASSERT_TRUE(db.GetParentPublicId(a, *manager, series2Id));  ASSERT_EQ("study", a);

  std::list<std::string> children;
  db.GetChildren(children, *manager, studyId);
  ASSERT_EQ(2u, children.size());
  db.GetChildren(children, *manager, seriesId);
  ASSERT_EQ(0u, children.size());
  db.GetChildren(children, *manager, series2Id);
  ASSERT_EQ(0u, children.size());

  std::list<std::string> cp;
  db.GetChildrenPublicId(cp, *manager, studyId);
  ASSERT_EQ(2u, cp.size());
  ASSERT_TRUE(cp.front() == "series" || cp.front() == "series2");
  ASSERT_TRUE(cp.back() == "series" || cp.back() == "series2");
  ASSERT_NE(cp.front(), cp.back());

  std::list<std::string> pub;
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Patient);
  ASSERT_EQ(0u, pub.size());
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Study);
  ASSERT_EQ(1u, pub.size());
  ASSERT_EQ("study", pub.front());
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Series);
  ASSERT_EQ(2u, pub.size());
  ASSERT_TRUE(pub.front() == "series" || pub.front() == "series2");
  ASSERT_TRUE(pub.back() == "series" || pub.back() == "series2");
  ASSERT_NE(pub.front(), pub.back());

  std::list<int64_t> ci;
  db.GetChildrenInternalId(ci, *manager, studyId);
  ASSERT_EQ(2u, ci.size());
  ASSERT_TRUE(ci.front() == seriesId || ci.front() == series2Id);
  ASSERT_TRUE(ci.back() == seriesId || ci.back() == series2Id);
  ASSERT_NE(ci.front(), ci.back());

  db.SetMetadata(*manager, studyId, Orthanc::MetadataType_ModifiedFrom, "modified", 42);
  db.SetMetadata(*manager, studyId, Orthanc::MetadataType_LastUpdate, "update2", 43);
  int64_t revision = -1;
  ASSERT_FALSE(db.LookupMetadata(a, revision, *manager, seriesId, Orthanc::MetadataType_LastUpdate));
  ASSERT_TRUE(db.LookupMetadata(a, revision, *manager, studyId, Orthanc::MetadataType_LastUpdate));
  ASSERT_EQ("update2", a);

#if HAS_REVISIONS == 1
  ASSERT_EQ(43, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  db.SetMetadata(*manager, studyId, Orthanc::MetadataType_LastUpdate, reinterpret_cast<const char*>(UTF8), 44);
  ASSERT_TRUE(db.LookupMetadata(a, revision, *manager, studyId, Orthanc::MetadataType_LastUpdate));
  ASSERT_STREQ(reinterpret_cast<const char*>(UTF8), a.c_str());

#if HAS_REVISIONS == 1
  ASSERT_EQ(44, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  std::list<int32_t> md;
  db.ListAvailableMetadata(md, *manager, studyId);
  ASSERT_EQ(2u, md.size());
  ASSERT_TRUE(md.front() == Orthanc::MetadataType_ModifiedFrom || md.back() == Orthanc::MetadataType_ModifiedFrom);
  ASSERT_TRUE(md.front() == Orthanc::MetadataType_LastUpdate || md.back() == Orthanc::MetadataType_LastUpdate);
  std::string mdd;
  ASSERT_TRUE(db.LookupMetadata(mdd, revision, *manager, studyId, Orthanc::MetadataType_ModifiedFrom));
  ASSERT_EQ("modified", mdd);

#if HAS_REVISIONS == 1
  ASSERT_EQ(42, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  ASSERT_TRUE(db.LookupMetadata(mdd, revision, *manager, studyId, Orthanc::MetadataType_LastUpdate));
  ASSERT_EQ(reinterpret_cast<const char*>(UTF8), mdd);

#if HAS_REVISIONS == 1
  ASSERT_EQ(44, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  db.ListAvailableMetadata(md, *manager, seriesId);
  ASSERT_EQ(0u, md.size());

  ASSERT_TRUE(db.LookupMetadata(a, revision, *manager, studyId, Orthanc::MetadataType_LastUpdate));
  db.DeleteMetadata(*manager, studyId, Orthanc::MetadataType_LastUpdate);
  ASSERT_FALSE(db.LookupMetadata(a, revision, *manager, studyId, Orthanc::MetadataType_LastUpdate));
  db.DeleteMetadata(*manager, seriesId, Orthanc::MetadataType_LastUpdate);
  ASSERT_FALSE(db.LookupMetadata(a, revision, *manager, studyId, Orthanc::MetadataType_LastUpdate));

  db.ListAvailableMetadata(md, *manager, studyId);
  ASSERT_EQ(1u, md.size());
  ASSERT_EQ(Orthanc::MetadataType_ModifiedFrom, md.front());

  ASSERT_EQ(0u, db.GetTotalCompressedSize(*manager));
  ASSERT_EQ(0u, db.GetTotalUncompressedSize(*manager));


  std::list<int32_t> fc;

  OrthancPluginAttachment att1;
  att1.uuid = "uuid1";
  att1.contentType = Orthanc::FileContentType_Dicom;
  att1.uncompressedSize = 42;
  att1.uncompressedHash = "md5_1";
  att1.compressionType = Orthanc::CompressionType_None;
  att1.compressedSize = 42;
  att1.compressedHash = "md5_1";
    
  OrthancPluginAttachment att2;
  att2.uuid = "uuid2";
  att2.contentType = Orthanc::FileContentType_DicomAsJson;
  att2.uncompressedSize = 4242;
  att2.uncompressedHash = "md5_2";
  att2.compressionType = Orthanc::CompressionType_None;
  att2.compressedSize = 4242;
  att2.compressedHash = "md5_2";
    
#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
  if (db.HasAttachmentCustomDataSupport())
  {
    db.AddAttachment(*manager, studyId, att1, 42, "my_custom_data");
    db.ListAvailableAttachments(fc, *manager, studyId);
  }
  else
#endif
  {
    db.AddAttachment(*manager, studyId, att1, 42);
  }

  db.ListAvailableAttachments(fc, *manager, studyId);
  ASSERT_EQ(1u, fc.size());
  ASSERT_EQ(Orthanc::FileContentType_Dicom, fc.front());
  db.AddAttachment(*manager, studyId, att2, 43);
  db.ListAvailableAttachments(fc, *manager, studyId);
  ASSERT_EQ(2u, fc.size());
  ASSERT_FALSE(db.LookupAttachment(*output, revision, *manager, seriesId, Orthanc::FileContentType_Dicom));

#if ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA == 1
  if (db.HasAttachmentCustomDataSupport())
  {
    std::string s;
    ASSERT_THROW(db.GetAttachmentCustomData(s, *manager, "nope"), Orthanc::OrthancException);

    db.GetAttachmentCustomData(s, *manager, "uuid1");
    ASSERT_EQ("my_custom_data", s);

    db.GetAttachmentCustomData(s, *manager, "uuid2");
    ASSERT_TRUE(s.empty());

    {
      std::string blob;
      FillBlob(blob);
      db.SetAttachmentCustomData(*manager, "uuid1", blob);
    }

    db.GetAttachmentCustomData(s, *manager, "uuid1");
    CheckBlob(s);

    db.SetAttachmentCustomData(*manager, "uuid1", "");
    db.GetAttachmentCustomData(s, *manager, "uuid1");
    ASSERT_TRUE(s.empty());
  }
#endif

  ASSERT_EQ(4284u, db.GetTotalCompressedSize(*manager));
  ASSERT_EQ(4284u, db.GetTotalUncompressedSize(*manager));

  expectedAttachment.reset(new OrthancPluginAttachment);
  expectedAttachment->uuid = "uuid1";
  expectedAttachment->contentType = Orthanc::FileContentType_Dicom;
  expectedAttachment->uncompressedSize = 42;
  expectedAttachment->uncompressedHash = "md5_1";
  expectedAttachment->compressionType = Orthanc::CompressionType_None;
  expectedAttachment->compressedSize = 42;
  expectedAttachment->compressedHash = "md5_1";
  ASSERT_TRUE(db.LookupAttachment(*output, revision, *manager, studyId, Orthanc::FileContentType_Dicom));

#if HAS_REVISIONS == 1
  ASSERT_EQ(42, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  expectedAttachment.reset(new OrthancPluginAttachment);
  expectedAttachment->uuid = "uuid2";
  expectedAttachment->contentType = Orthanc::FileContentType_DicomAsJson;
  expectedAttachment->uncompressedSize = 4242;
  expectedAttachment->uncompressedHash = "md5_2";
  expectedAttachment->compressionType = Orthanc::CompressionType_None;
  expectedAttachment->compressedSize = 4242;
  expectedAttachment->compressedHash = "md5_2";
  revision = -1;
  ASSERT_TRUE(db.LookupAttachment(*output, revision, *manager, studyId, Orthanc::FileContentType_DicomAsJson));

#if HAS_REVISIONS == 1
  ASSERT_EQ(43, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  db.ListAvailableAttachments(fc, *manager, seriesId);
  ASSERT_EQ(0u, fc.size());
  db.DeleteAttachment(*output, *manager, studyId, Orthanc::FileContentType_Dicom);
  db.ListAvailableAttachments(fc, *manager, studyId);
  ASSERT_EQ(1u, fc.size());
  ASSERT_EQ(Orthanc::FileContentType_DicomAsJson, fc.front());
  db.DeleteAttachment(*output, *manager, studyId, Orthanc::FileContentType_DicomAsJson);
  db.ListAvailableAttachments(fc, *manager, studyId);
  ASSERT_EQ(0u, fc.size());

  db.SetIdentifierTag(*manager, studyId, 0x0010, 0x0020, "patient");
  db.SetIdentifierTag(*manager, studyId, 0x0020, 0x000d, "study");
  db.SetMainDicomTag(*manager, studyId, 0x0010, 0x0020, "patient");
  db.SetMainDicomTag(*manager, studyId, 0x0020, 0x000d, "study");
  db.SetMainDicomTag(*manager, studyId, 0x0008, 0x1030, reinterpret_cast<const char*>(UTF8));

  expectedDicomTags.clear();
  expectedDicomTags.push_back(OrthancPluginDicomTag());
  expectedDicomTags.back().group = 0x0010;
  expectedDicomTags.back().element = 0x0020;
  expectedDicomTags.back().value = "patient";
  expectedDicomTags.push_back(OrthancPluginDicomTag());
  expectedDicomTags.back().group = 0x0020;
  expectedDicomTags.back().element = 0x000d;
  expectedDicomTags.back().value = "study";
  expectedDicomTags.push_back(OrthancPluginDicomTag());
  expectedDicomTags.back().group = 0x0008;
  expectedDicomTags.back().element = 0x1030;
  expectedDicomTags.back().value = reinterpret_cast<const char*>(UTF8);

  countDicomTags = 0;
  db.GetMainDicomTags(*output, *manager, studyId);
  ASSERT_EQ(3u, countDicomTags);

  db.LookupIdentifier(ci, *manager, OrthancPluginResourceType_Study, 0x0010, 0x0020, 
                      OrthancPluginIdentifierConstraint_Equal, "patient");
  ASSERT_EQ(1u, ci.size());
  ASSERT_EQ(studyId, ci.front());
  db.LookupIdentifier(ci, *manager, OrthancPluginResourceType_Study, 0x0010, 0x0020, 
                      OrthancPluginIdentifierConstraint_Equal, "study");
  ASSERT_EQ(0u, ci.size());


  db.LogExportedResource(*manager, OrthancPluginResourceType_Study, "id", "remote", "date",
                         "patient", "study", "series", "instance");

  expectedExported.reset(new OrthancPluginExportedResource());
  expectedExported->seq = -1;
  expectedExported->resourceType = OrthancPluginResourceType_Study;
  expectedExported->publicId = "id";
  expectedExported->modality = "remote";
  expectedExported->date = "date";
  expectedExported->patientId = "patient";
  expectedExported->studyInstanceUid = "study";
  expectedExported->seriesInstanceUid = "series";
  expectedExported->sopInstanceUid = "instance";

  bool done;
  db.GetExportedResources(*output, done, *manager, 0, 10);
  

  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Patient); ASSERT_EQ(0u, pub.size());
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Study); ASSERT_EQ(1u, pub.size());
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Series); ASSERT_EQ(2u, pub.size());
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Instance); ASSERT_EQ(0u, pub.size());
  ASSERT_EQ(3u, db.GetAllResourcesCount(*manager));

  #if CAN_TEST_PATIENT_PROTECTION == 1
    ASSERT_EQ(0u, db.GetUnprotectedPatientsCount(*manager));  // No patient was inserted
  #endif

  ASSERT_TRUE(db.IsExistingResource(*manager, series2Id));

  {
    // A transaction is needed here for MySQL, as it was not possible
    // to implement recursive deletion of resources using pure SQL
    // statements
    manager->StartTransaction(TransactionType_ReadWrite);

    deletedAttachments.clear();
    deletedResources.clear();
    remainingAncestor.reset();
    
    db.DeleteResource(*output, *manager, series2Id);

    ASSERT_EQ(0u, deletedAttachments.size());
    ASSERT_EQ(1u, deletedResources.size());
    ASSERT_EQ(OrthancPluginResourceType_Series, deletedResources["series2"]);
    ASSERT_TRUE(remainingAncestor.get() != NULL);
    ASSERT_EQ("study", remainingAncestor->first);
    ASSERT_EQ(OrthancPluginResourceType_Study, remainingAncestor->second);
    
    manager->CommitTransaction();
  }
  
  deletedAttachments.clear();
  deletedResources.clear();
  remainingAncestor.reset();

  ASSERT_FALSE(db.IsExistingResource(*manager, series2Id));
  ASSERT_TRUE(db.IsExistingResource(*manager, studyId));
  ASSERT_TRUE(db.IsExistingResource(*manager, seriesId));
  ASSERT_EQ(2u, db.GetAllResourcesCount(*manager));

  {
    // An explicit transaction is needed here
    manager->StartTransaction(TransactionType_ReadWrite);
    db.DeleteResource(*output, *manager, studyId);  // delete the study that only has one series left -> 2 resources shall be deleted
    manager->CommitTransaction();
  }

  ASSERT_EQ(0u, db.GetAllResourcesCount(*manager));
  ASSERT_FALSE(db.IsExistingResource(*manager, studyId));
  ASSERT_FALSE(db.IsExistingResource(*manager, seriesId));
  ASSERT_FALSE(db.IsExistingResource(*manager, series2Id));

  ASSERT_EQ(0u, deletedAttachments.size());
  ASSERT_EQ(2u, deletedResources.size());
  ASSERT_EQ(OrthancPluginResourceType_Series, deletedResources["series"]);
  ASSERT_EQ(OrthancPluginResourceType_Study, deletedResources["study"]);
  ASSERT_FALSE(remainingAncestor.get() != NULL);
  
  ASSERT_EQ(0u, db.GetAllResourcesCount(*manager));
#if CAN_TEST_PATIENT_PROTECTION == 1
  ASSERT_EQ(0u, db.GetUnprotectedPatientsCount(*manager));
  int64_t p1 = db.CreateResource(*manager, "patient1", OrthancPluginResourceType_Patient);
  int64_t p2 = db.CreateResource(*manager, "patient2", OrthancPluginResourceType_Patient);
  int64_t p3 = db.CreateResource(*manager, "patient3", OrthancPluginResourceType_Patient);
  ASSERT_EQ(3u, db.GetUnprotectedPatientsCount(*manager));
  int64_t r;
  ASSERT_TRUE(db.SelectPatientToRecycle(r, *manager));
  ASSERT_EQ(p1, r);
  ASSERT_TRUE(db.SelectPatientToRecycle(r, *manager, p1));
  ASSERT_EQ(p2, r);
  ASSERT_FALSE(db.IsProtectedPatient(*manager, p1));
  db.SetProtectedPatient(*manager, p1, true);
  ASSERT_TRUE(db.IsProtectedPatient(*manager, p1));
  ASSERT_TRUE(db.SelectPatientToRecycle(r, *manager));
  ASSERT_EQ(p2, r);
  db.SetProtectedPatient(*manager, p1, false);
  ASSERT_FALSE(db.IsProtectedPatient(*manager, p1));
  ASSERT_TRUE(db.SelectPatientToRecycle(r, *manager));
  ASSERT_EQ(p2, r);

  {
    // An explicit transaction is needed here
    manager->StartTransaction(TransactionType_ReadWrite);
    db.DeleteResource(*output, *manager, p2);
    manager->CommitTransaction();
  }

  ASSERT_TRUE(db.SelectPatientToRecycle(r, *manager, p3));
  ASSERT_EQ(p1, r);

  {
    manager->StartTransaction(TransactionType_ReadWrite);
    db.DeleteResource(*output, *manager, p1);
    db.DeleteResource(*output, *manager, p3);
    manager->CommitTransaction();
  }
#endif

#if CAN_TEST_LARGE_PROPERTIES == 1
  {
    // Test creating a large property of 16MB (large properties are
    // notably necessary to serialize jobs)
    // https://groups.google.com/g/orthanc-users/c/1Y3nTBdr0uE/m/K7PA5pboAgAJ
    std::string longProperty;
    longProperty.resize(16 * 1024 * 1024);
    for (size_t i = 0; i < longProperty.size(); i++)
    {
      longProperty[i] = 'A' + (i % 26);
    }

    db.SetGlobalProperty(*manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8, longProperty.c_str());

    // The following line fails on MySQL 4.0 because the "value"
    // column in "ServerProperties" is "TEXT" instead of "LONGTEXT"
    db.SetGlobalProperty(*manager, "some-server", Orthanc::GlobalProperty_DatabaseInternal8, longProperty.c_str());

    ASSERT_TRUE(db.LookupGlobalProperty(a, *manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8));
    ASSERT_EQ(longProperty, a);

    a.clear();
    ASSERT_TRUE(db.LookupGlobalProperty(a, *manager, "some-server", Orthanc::GlobalProperty_DatabaseInternal8));
    ASSERT_EQ(longProperty, a);
  }
#endif

#if CAN_TEST_CASCADE_DELETE == 1
  for (size_t level = 0; level < 4; level++)
  {
    for (size_t attachmentLevel = 0; attachmentLevel < 4; attachmentLevel++)
    {
      // Test cascade up to the "patient" level
      ASSERT_EQ(0u, db.GetAllResourcesCount(*manager));

      std::vector<int64_t> resources;
      resources.push_back(db.CreateResource(*manager, "patient", OrthancPluginResourceType_Patient));
      resources.push_back(db.CreateResource(*manager, "study", OrthancPluginResourceType_Study));
      resources.push_back(db.CreateResource(*manager, "series", OrthancPluginResourceType_Series));
      resources.push_back(db.CreateResource(*manager, "instance", OrthancPluginResourceType_Instance));

      OrthancPluginAttachment d;
      d.uuid = "attachment";
      d.contentType = Orthanc::FileContentType_DicomAsJson;
      d.uncompressedSize = 4242;
      d.uncompressedHash = "md5";
      d.compressionType = Orthanc::CompressionType_None;
      d.compressedSize = 4242;
      d.compressedHash = "md5";
      db.AddAttachment(*manager, resources[attachmentLevel], d, 42);
    
      db.AttachChild(*manager, resources[0], resources[1]);
      db.AttachChild(*manager, resources[1], resources[2]);
      db.AttachChild(*manager, resources[2], resources[3]);
      ASSERT_EQ(4u, db.GetAllResourcesCount(*manager));

      deletedAttachments.clear();
      deletedResources.clear();
      remainingAncestor.reset();
    
      {
        manager->StartTransaction(TransactionType_ReadWrite);
        db.DeleteResource(*output, *manager, resources[level]);
        manager->CommitTransaction();
      }
    
      ASSERT_EQ(1u, deletedAttachments.size());
      ASSERT_EQ("attachment", *deletedAttachments.begin());
      ASSERT_EQ(4u, deletedResources.size());
      ASSERT_EQ(OrthancPluginResourceType_Patient, deletedResources["patient"]);
      ASSERT_EQ(OrthancPluginResourceType_Study, deletedResources["study"]);
      ASSERT_EQ(OrthancPluginResourceType_Series, deletedResources["series"]);
      ASSERT_EQ(OrthancPluginResourceType_Instance, deletedResources["instance"]);
      ASSERT_TRUE(remainingAncestor.get() == NULL);
    }
  }

#endif

#if ORTHANC_ENABLE_POSTGRESQL == 0 && CAN_TEST_CASCADE_DELETE == 1  // In PostgreSQL, remaining ancestor are implemented in the PostgreSQLIndex, not in the IndexBackend.  Note: they are tested in the integration tests

  for (size_t level = 1; level < 4; level++)
  {
    for (size_t attachmentLevel = 0; attachmentLevel < 4; attachmentLevel++)
    {
      // Test remaining ancestor
      ASSERT_EQ(0u, db.GetAllResourcesCount(*manager));

      std::vector<int64_t> resources;
      resources.push_back(db.CreateResource(*manager, "patient", OrthancPluginResourceType_Patient));
      resources.push_back(db.CreateResource(*manager, "study", OrthancPluginResourceType_Study));
      resources.push_back(db.CreateResource(*manager, "series", OrthancPluginResourceType_Series));
      resources.push_back(db.CreateResource(*manager, "instance", OrthancPluginResourceType_Instance));

      int64_t unrelated = db.CreateResource(*manager, "unrelated", OrthancPluginResourceType_Patient);
      int64_t remaining = db.CreateResource(*manager, "remaining", static_cast<OrthancPluginResourceType>(level));

      db.AttachChild(*manager, resources[0], resources[1]);
      db.AttachChild(*manager, resources[1], resources[2]);
      db.AttachChild(*manager, resources[2], resources[3]);
      db.AttachChild(*manager, resources[level - 1], remaining);
      ASSERT_EQ(6u, db.GetAllResourcesCount(*manager));

      OrthancPluginAttachment d;
      d.uuid = "attachment";
      d.contentType = Orthanc::FileContentType_DicomAsJson;
      d.uncompressedSize = 4242;
      d.uncompressedHash = "md5";
      d.compressionType = Orthanc::CompressionType_None;
      d.compressedSize = 4242;
      d.compressedHash = "md5";
      db.AddAttachment(*manager, resources[attachmentLevel], d, 42);

      deletedAttachments.clear();
      d.uuid = "attachment2";
      db.DeleteAttachment(*output, *manager, resources[attachmentLevel], Orthanc::FileContentType_DicomAsJson);
      ASSERT_EQ(1u, deletedAttachments.size());
      ASSERT_EQ("attachment", *deletedAttachments.begin());
      
      db.AddAttachment(*manager, resources[attachmentLevel], d, 43);
      
      deletedAttachments.clear();
      deletedResources.clear();
      remainingAncestor.reset();
    
      {
        manager->StartTransaction(TransactionType_ReadWrite);
        db.DeleteResource(*output, *manager, resources[3]);  // delete instance
        manager->CommitTransaction();
      }

      if (attachmentLevel < level)
      {
        ASSERT_EQ(0u, deletedAttachments.size());
      }
      else
      {
        ASSERT_EQ(1u, deletedAttachments.size());
        ASSERT_EQ("attachment2", *deletedAttachments.begin());
      }
      
      ASSERT_EQ(OrthancPluginResourceType_Instance, deletedResources["instance"]);
    
      ASSERT_TRUE(remainingAncestor.get() != NULL);
    
      switch (level)
      {
        case 1:
          ASSERT_EQ(3u, deletedResources.size());
          ASSERT_EQ(OrthancPluginResourceType_Study, deletedResources["study"]);
          ASSERT_EQ(OrthancPluginResourceType_Series, deletedResources["series"]);
          ASSERT_EQ("patient", remainingAncestor->first);
          ASSERT_EQ(OrthancPluginResourceType_Patient, remainingAncestor->second);
          break;

        case 2:
          ASSERT_EQ(2u, deletedResources.size());
          ASSERT_EQ(OrthancPluginResourceType_Series, deletedResources["series"]);
          ASSERT_EQ("study", remainingAncestor->first);
          ASSERT_EQ(OrthancPluginResourceType_Study, remainingAncestor->second);
          break;

        case 3:
          ASSERT_EQ(1u, deletedResources.size());
          ASSERT_EQ("series", remainingAncestor->first);
          ASSERT_EQ(OrthancPluginResourceType_Series, remainingAncestor->second);
          break;

        default:
          ORTHANC_PLUGINS_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
      }
    
      {
        manager->StartTransaction(TransactionType_ReadWrite);
        db.DeleteResource(*output, *manager, resources[0]);
        db.DeleteResource(*output, *manager, unrelated);
        manager->CommitTransaction();
      }
    }
  }
#endif


#if ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES == 1
  if (db.HasKeyValueStores())
  {
    manager->StartTransaction(TransactionType_ReadWrite);

    std::set<std::string> keys;
    ListKeys(keys, db, *manager, "test");
    ASSERT_EQ(0u, keys.size());

    std::string s;
    ASSERT_FALSE(db.GetKeyValue(s, *manager, "test", "hello"));
    db.DeleteKeyValue(*manager, s, "test");

    db.StoreKeyValue(*manager, "test", "hello", "world");
    db.StoreKeyValue(*manager, "another", "hello", "world");
    ListKeys(keys, db, *manager, "test");
    ASSERT_EQ(1u, keys.size());
    ASSERT_EQ("hello", *keys.begin());
    ASSERT_TRUE(db.GetKeyValue(s, *manager, "test", "hello"));  ASSERT_EQ("world", s);

    db.StoreKeyValue(*manager, "test", "hello", "overwritten");
    ListKeys(keys, db, *manager, "test");
    ASSERT_EQ(1u, keys.size());
    ASSERT_EQ("hello", *keys.begin());
    ASSERT_TRUE(db.GetKeyValue(s, *manager, "test", "hello"));  ASSERT_EQ("overwritten", s);

    db.StoreKeyValue(*manager, "test", "hello2", "world2");
    db.StoreKeyValue(*manager, "test", "hello3", "world3");

    ListKeys(keys, db, *manager, "test");
    ASSERT_EQ(3u, keys.size());
    ASSERT_TRUE(keys.find("hello") != keys.end());
    ASSERT_TRUE(keys.find("hello2") != keys.end());
    ASSERT_TRUE(keys.find("hello3") != keys.end());
    ASSERT_TRUE(db.GetKeyValue(s, *manager, "test", "hello"));   ASSERT_EQ("overwritten", s);
    ASSERT_TRUE(db.GetKeyValue(s, *manager, "test", "hello2"));  ASSERT_EQ("world2", s);
    ASSERT_TRUE(db.GetKeyValue(s, *manager, "test", "hello3"));  ASSERT_EQ("world3", s);

    db.DeleteKeyValue(*manager, "test", "hello2");

    ListKeys(keys, db, *manager, "test");
    ASSERT_EQ(2u, keys.size());
    ASSERT_TRUE(keys.find("hello") != keys.end());
    ASSERT_TRUE(keys.find("hello3") != keys.end());
    ASSERT_TRUE(db.GetKeyValue(s, *manager, "test", "hello"));   ASSERT_EQ("overwritten", s);
    ASSERT_FALSE(db.GetKeyValue(s, *manager, "test", "hello2"));
    ASSERT_TRUE(db.GetKeyValue(s, *manager, "test", "hello3"));  ASSERT_EQ("world3", s);

    db.DeleteKeyValue(*manager, "test", "nope");
    db.DeleteKeyValue(*manager, "test", "hello");
    db.DeleteKeyValue(*manager, "test", "hello3");

    ListKeys(keys, db, *manager, "test");
    ASSERT_EQ(0u, keys.size());

    {
      std::string blob;
      FillBlob(blob);
      db.StoreKeyValue(*manager, "test", "blob", blob); // Storing binary values
    }

    ASSERT_TRUE(db.GetKeyValue(s, *manager, "test", "blob"));
    CheckBlob(s);
    db.DeleteKeyValue(*manager, "test", "blob");
    ASSERT_FALSE(db.GetKeyValue(s, *manager, "test", "blob"));

    manager->CommitTransaction();
  }
#endif


#if ORTHANC_PLUGINS_HAS_QUEUES == 1
  if (db.HasQueues())
  {
    manager->StartTransaction(TransactionType_ReadWrite);

    ASSERT_EQ(0u, db.GetQueueSize(*manager, "test"));
    db.EnqueueValue(*manager, "test", "a");
    db.EnqueueValue(*manager, "another", "hello");
    ASSERT_EQ(1u, db.GetQueueSize(*manager, "test"));
    db.EnqueueValue(*manager, "test", "b");
    ASSERT_EQ(2u, db.GetQueueSize(*manager, "test"));
    db.EnqueueValue(*manager, "test", "c");
    ASSERT_EQ(3u, db.GetQueueSize(*manager, "test"));

    std::string s;
    ASSERT_FALSE(db.DequeueValue(s, *manager, "nope", false));
    ASSERT_TRUE(db.DequeueValue(s, *manager, "test", true));  ASSERT_EQ("a", s);
    ASSERT_EQ(2u, db.GetQueueSize(*manager, "test"));
    ASSERT_TRUE(db.DequeueValue(s, *manager, "test", true));  ASSERT_EQ("b", s);
    ASSERT_EQ(1u, db.GetQueueSize(*manager, "test"));
    ASSERT_TRUE(db.DequeueValue(s, *manager, "test", true));  ASSERT_EQ("c", s);
    ASSERT_EQ(0u, db.GetQueueSize(*manager, "test"));
    ASSERT_FALSE(db.DequeueValue(s, *manager, "test", true));

    db.EnqueueValue(*manager, "test", "a");
    db.EnqueueValue(*manager, "test", "b");
    db.EnqueueValue(*manager, "test", "c");

    ASSERT_TRUE(db.DequeueValue(s, *manager, "test", false));  ASSERT_EQ("c", s);
    ASSERT_TRUE(db.DequeueValue(s, *manager, "test", false));  ASSERT_EQ("b", s);
    ASSERT_TRUE(db.DequeueValue(s, *manager, "test", false));  ASSERT_EQ("a", s);
    ASSERT_FALSE(db.DequeueValue(s, *manager, "test", false));

    {
      std::string blob;
      FillBlob(blob);
      db.EnqueueValue(*manager, "test", blob); // Storing binary values
    }

    ASSERT_TRUE(db.DequeueValue(s, *manager, "test", true));
    CheckBlob(s);

    ASSERT_FALSE(db.DequeueValue(s, *manager, "test", true));

    ASSERT_EQ(1u, db.GetQueueSize(*manager, "another"));

    manager->CommitTransaction();
  }
#endif

#if ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE == 1
  if (db.HasReserveQueueValue())
  {
    std::string value;
    uint64_t valueIdA, valueIdB, valueIdC, valueIdD, valueIdE;

    {
      manager->StartTransaction(TransactionType_ReadWrite);

      db.EnqueueValue(*manager, "test", "a");
      db.EnqueueValue(*manager, "test", "b");
      db.EnqueueValue(*manager, "test", "c");
      db.EnqueueValue(*manager, "test", "d");
      db.EnqueueValue(*manager, "test", "e");

      ASSERT_EQ(5u, db.GetQueueSize(*manager, "test"));

      ASSERT_TRUE(db.ReserveQueueValue(value, valueIdA, *manager, "test", true, 1000));
      ASSERT_EQ("a", value);
      ASSERT_TRUE(db.ReserveQueueValue(value, valueIdB, *manager, "test", true, 1));
      ASSERT_EQ("b", value);
      ASSERT_TRUE(db.ReserveQueueValue(value, valueIdE, *manager, "test", false, 1));
      ASSERT_EQ("e", value);
      ASSERT_TRUE(db.ReserveQueueValue(value, valueIdD, *manager, "test", false, 1));
      ASSERT_EQ("d", value);
      manager->CommitTransaction(); 
    }

    {
      manager->StartTransaction(TransactionType_ReadWrite);

      db.AcknowledgeQueueValue(*manager, "test", valueIdA);
      db.AcknowledgeQueueValue(*manager, "test", valueIdE);
      manager->CommitTransaction(); 
    }

    Orthanc::SystemToolbox::USleep(2000000);  // Wait 2 seconds -> b and d should be released

    {
      manager->StartTransaction(TransactionType_ReadWrite);
      ASSERT_TRUE(db.ReserveQueueValue(value, valueIdB, *manager, "test", true, 1));
      ASSERT_EQ("b", value);
      ASSERT_TRUE(db.ReserveQueueValue(value, valueIdD, *manager, "test", false, 1));
      ASSERT_EQ("d", value);
      ASSERT_TRUE(db.ReserveQueueValue(value, valueIdC, *manager, "test", false, 1));
      ASSERT_EQ("c", value);

      uint64_t valueIdFail;
      ASSERT_FALSE(db.ReserveQueueValue(value, valueIdFail, *manager, "test", false, 1));

      manager->CommitTransaction();
    }

    Orthanc::SystemToolbox::USleep(2000000);  // Wait 2 seconds -> b, c and d should be released

    // try to acknowledge a value after it has expired
    {
      manager->StartTransaction(TransactionType_ReadWrite);

      ASSERT_THROW(db.AcknowledgeQueueValue(*manager, "test", valueIdC), Orthanc::OrthancException);

      manager->CommitTransaction();
    }

  }
#endif

  manager->Close();
}
