// Taken from https://hg.orthanc-server.com/orthanc-databases/


#pragma once

#include "DatabaseBackendAdapterV2.h"
#include "GlobalProperties.h"

#include <Compatibility.h>  // For std::unique_ptr<>

#include <orthanc/OrthancCDatabasePlugin.h>

#include <gtest/gtest.h>
#include <list>

#if !defined(ORTHANC_DATABASE_VERSION)
// This happens if using the Orthanc framework system-wide library
#  define ORTHANC_DATABASE_VERSION 6
#endif


# define HAS_REVISIONS 1


namespace Orthanc
{
  /**
   * Mock enumeration inspired from the source code of Orthanc... only
   * for use in the unit tests!
   * https://hg.orthanc-server.com/orthanc/file/default/OrthancServer/Sources/ServerEnumerations.h
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



static OrthancPluginErrorCode InvokeService(struct _OrthancPluginContext_t* context,
                                            _OrthancPluginService service,
                                            const void* params)
{
  switch (service)
  {
    case _OrthancPluginService_DatabaseAnswer:
    {
      const _OrthancPluginDatabaseAnswer& answer =
        *reinterpret_cast<const _OrthancPluginDatabaseAnswer*>(params);

      switch (answer.type)
      {
        case _OrthancPluginDatabaseAnswerType_Attachment:
        {
          const OrthancPluginAttachment& attachment =
            *reinterpret_cast<const OrthancPluginAttachment*>(answer.valueGeneric);
          CheckAttachment(attachment);
          break;
        }

        case _OrthancPluginDatabaseAnswerType_ExportedResource:
        {
          const OrthancPluginExportedResource& attachment =
            *reinterpret_cast<const OrthancPluginExportedResource*>(answer.valueGeneric);
          CheckExportedResource(attachment);
          break;
        }

        case _OrthancPluginDatabaseAnswerType_DicomTag:
        {
          const OrthancPluginDicomTag& tag =
            *reinterpret_cast<const OrthancPluginDicomTag*>(answer.valueGeneric);
          CheckDicomTag(tag);
          countDicomTags++;
          break;
        }

        case _OrthancPluginDatabaseAnswerType_DeletedResource:
          deletedResources[answer.valueString] = static_cast<OrthancPluginResourceType>(answer.valueInt32);
          break;

        case _OrthancPluginDatabaseAnswerType_RemainingAncestor:
          remainingAncestor.reset(new std::pair<std::string, OrthancPluginResourceType>());
          *remainingAncestor = std::make_pair(answer.valueString, static_cast<OrthancPluginResourceType>(answer.valueInt32));
          break;

        case _OrthancPluginDatabaseAnswerType_DeletedAttachment:
          deletedAttachments.insert(reinterpret_cast<const OrthancPluginAttachment*>(answer.valueGeneric)->uuid);
          break;

        default:
          printf("Unhandled message: %d\n", answer.type);
          break;
      }

      return OrthancPluginErrorCode_Success;
    }

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


TEST(IndexBackend, Basic)
{
  using namespace OrthancDatabases;

  OrthancPluginContext context;
  context.pluginsManager = NULL;
  context.orthancVersion = "mainline";
  context.Free = ::free;
  context.InvokeService = InvokeService;

#if ORTHANC_ENABLE_POSTGRESQL == 1
  PostgreSQLIndex db(&context, globalParameters_);
  db.SetClearAll(true);
#elif ORTHANC_ENABLE_MYSQL == 1
  MySQLIndex db(&context, globalParameters_);
  db.SetClearAll(true);
#elif ORTHANC_ENABLE_ODBC == 1
  OdbcIndex db(&context, connectionString_);
#elif ORTHANC_ENABLE_SQLITE == 1  // Must be the last one
  SQLiteIndex db(&context);  // Open in memory
#else
#  error Unsupported database backend
#endif

  db.SetOutputFactory(new DatabaseBackendAdapterV2::Factory(&context, NULL));

  std::unique_ptr<DatabaseManager> manager(IndexBackend::CreateSingleDatabaseManager(db));

  std::unique_ptr<IDatabaseBackendOutput> output(db.CreateOutput());

  std::string s;
  ASSERT_TRUE(db.LookupGlobalProperty(s, *manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseSchemaVersion));
  ASSERT_EQ("6", s);

  db.SetGlobalProperty(*manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9, "Hello");
  ASSERT_TRUE(db.LookupGlobalProperty(s, *manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9));
  ASSERT_EQ("Hello", s);
  db.SetGlobalProperty(*manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9, "HelloWorld");
  ASSERT_TRUE(db.LookupGlobalProperty(s, *manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9));
  ASSERT_EQ("HelloWorld", s);

  ASSERT_EQ(0u, db.GetAllResourcesCount(*manager));
  ASSERT_EQ(0u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Patient));
  ASSERT_EQ(0u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Study));
  ASSERT_EQ(0u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Series));

  int64_t a = db.CreateResource(*manager, "study", OrthancPluginResourceType_Study);
  ASSERT_TRUE(db.IsExistingResource(*manager, a));
  ASSERT_FALSE(db.IsExistingResource(*manager, a + 1));

  int64_t b;
  OrthancPluginResourceType t;
  ASSERT_FALSE(db.LookupResource(b, t, *manager, "world"));
  ASSERT_TRUE(db.LookupResource(b, t, *manager, "study"));
  ASSERT_EQ(a, b);
  ASSERT_EQ(OrthancPluginResourceType_Study, t);

  b = db.CreateResource(*manager, "series", OrthancPluginResourceType_Series);
  ASSERT_NE(a, b);

  ASSERT_EQ("study", db.GetPublicId(*manager, a));
  ASSERT_EQ("series", db.GetPublicId(*manager, b));
  ASSERT_EQ(OrthancPluginResourceType_Study, db.GetResourceType(*manager, a));
  ASSERT_EQ(OrthancPluginResourceType_Series, db.GetResourceType(*manager, b));

  db.AttachChild(*manager, a, b);

  int64_t c;
  ASSERT_FALSE(db.LookupParent(c, *manager, a));
  ASSERT_TRUE(db.LookupParent(c, *manager, b));
  ASSERT_EQ(a, c);

  c = db.CreateResource(*manager, "series2", OrthancPluginResourceType_Series);
  db.AttachChild(*manager, a, c);

  ASSERT_EQ(3u, db.GetAllResourcesCount(*manager));
  ASSERT_EQ(0u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Patient));
  ASSERT_EQ(1u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Study));
  ASSERT_EQ(2u, db.GetResourcesCount(*manager, OrthancPluginResourceType_Series));

  ASSERT_FALSE(db.GetParentPublicId(s, *manager, a));
  ASSERT_TRUE(db.GetParentPublicId(s, *manager, b));  ASSERT_EQ("study", s);
  ASSERT_TRUE(db.GetParentPublicId(s, *manager, c));  ASSERT_EQ("study", s);

  std::list<std::string> children;
  db.GetChildren(children, *manager, a);
  ASSERT_EQ(2u, children.size());
  db.GetChildren(children, *manager, b);
  ASSERT_EQ(0u, children.size());
  db.GetChildren(children, *manager, c);
  ASSERT_EQ(0u, children.size());

  std::list<std::string> cp;
  db.GetChildrenPublicId(cp, *manager, a);
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
  db.GetChildrenInternalId(ci, *manager, a);
  ASSERT_EQ(2u, ci.size());
  ASSERT_TRUE(ci.front() == b || ci.front() == c);
  ASSERT_TRUE(ci.back() == b || ci.back() == c);
  ASSERT_NE(ci.front(), ci.back());

  db.SetMetadata(*manager, a, Orthanc::MetadataType_ModifiedFrom, "modified", 42);
  db.SetMetadata(*manager, a, Orthanc::MetadataType_LastUpdate, "update2", 43);
  int64_t revision = -1;
  ASSERT_FALSE(db.LookupMetadata(s, revision, *manager, b, Orthanc::MetadataType_LastUpdate));
  ASSERT_TRUE(db.LookupMetadata(s, revision, *manager, a, Orthanc::MetadataType_LastUpdate));
  ASSERT_EQ("update2", s);

#if HAS_REVISIONS == 1
  ASSERT_EQ(43, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  db.SetMetadata(*manager, a, Orthanc::MetadataType_LastUpdate, reinterpret_cast<const char*>(UTF8), 44);
  ASSERT_TRUE(db.LookupMetadata(s, revision, *manager, a, Orthanc::MetadataType_LastUpdate));
  ASSERT_STREQ(reinterpret_cast<const char*>(UTF8), s.c_str());

#if HAS_REVISIONS == 1
  ASSERT_EQ(44, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  std::list<int32_t> md;
  db.ListAvailableMetadata(md, *manager, a);
  ASSERT_EQ(2u, md.size());
  ASSERT_TRUE(md.front() == Orthanc::MetadataType_ModifiedFrom || md.back() == Orthanc::MetadataType_ModifiedFrom);
  ASSERT_TRUE(md.front() == Orthanc::MetadataType_LastUpdate || md.back() == Orthanc::MetadataType_LastUpdate);
  std::string mdd;
  ASSERT_TRUE(db.LookupMetadata(mdd, revision, *manager, a, Orthanc::MetadataType_ModifiedFrom));
  ASSERT_EQ("modified", mdd);

#if HAS_REVISIONS == 1
  ASSERT_EQ(42, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  ASSERT_TRUE(db.LookupMetadata(mdd, revision, *manager, a, Orthanc::MetadataType_LastUpdate));
  ASSERT_EQ(reinterpret_cast<const char*>(UTF8), mdd);

#if HAS_REVISIONS == 1
  ASSERT_EQ(44, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  db.ListAvailableMetadata(md, *manager, b);
  ASSERT_EQ(0u, md.size());

  ASSERT_TRUE(db.LookupMetadata(s, revision, *manager, a, Orthanc::MetadataType_LastUpdate));
  db.DeleteMetadata(*manager, a, Orthanc::MetadataType_LastUpdate);
  ASSERT_FALSE(db.LookupMetadata(s, revision, *manager, a, Orthanc::MetadataType_LastUpdate));
  db.DeleteMetadata(*manager, b, Orthanc::MetadataType_LastUpdate);
  ASSERT_FALSE(db.LookupMetadata(s, revision, *manager, a, Orthanc::MetadataType_LastUpdate));

  db.ListAvailableMetadata(md, *manager, a);
  ASSERT_EQ(1u, md.size());
  ASSERT_EQ(Orthanc::MetadataType_ModifiedFrom, md.front());

  ASSERT_EQ(0u, db.GetTotalCompressedSize(*manager));
  ASSERT_EQ(0u, db.GetTotalUncompressedSize(*manager));


  std::list<int32_t> fc;

  OrthancPluginAttachment a1;
  a1.uuid = "uuid1";
  a1.contentType = Orthanc::FileContentType_Dicom;
  a1.uncompressedSize = 42;
  a1.uncompressedHash = "md5_1";
  a1.compressionType = Orthanc::CompressionType_None;
  a1.compressedSize = 42;
  a1.compressedHash = "md5_1";

  OrthancPluginAttachment a2;
  a2.uuid = "uuid2";
  a2.contentType = Orthanc::FileContentType_DicomAsJson;
  a2.uncompressedSize = 4242;
  a2.uncompressedHash = "md5_2";
  a2.compressionType = Orthanc::CompressionType_None;
  a2.compressedSize = 4242;
  a2.compressedHash = "md5_2";

  db.AddAttachment(*manager, a, a1, 42);
  db.ListAvailableAttachments(fc, *manager, a);
  ASSERT_EQ(1u, fc.size());
  ASSERT_EQ(Orthanc::FileContentType_Dicom, fc.front());
  db.AddAttachment(*manager, a, a2, 43);
  db.ListAvailableAttachments(fc, *manager, a);
  ASSERT_EQ(2u, fc.size());
  ASSERT_FALSE(db.LookupAttachment(*output, revision, *manager, b, Orthanc::FileContentType_Dicom));

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
  ASSERT_TRUE(db.LookupAttachment(*output, revision, *manager, a, Orthanc::FileContentType_Dicom));

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
  ASSERT_TRUE(db.LookupAttachment(*output, revision, *manager, a, Orthanc::FileContentType_DicomAsJson));

#if HAS_REVISIONS == 1
  ASSERT_EQ(43, revision);
#else
  ASSERT_EQ(0, revision);
#endif

  db.ListAvailableAttachments(fc, *manager, b);
  ASSERT_EQ(0u, fc.size());
  db.DeleteAttachment(*output, *manager, a, Orthanc::FileContentType_Dicom);
  db.ListAvailableAttachments(fc, *manager, a);
  ASSERT_EQ(1u, fc.size());
  ASSERT_EQ(Orthanc::FileContentType_DicomAsJson, fc.front());
  db.DeleteAttachment(*output, *manager, a, Orthanc::FileContentType_DicomAsJson);
  db.ListAvailableAttachments(fc, *manager, a);
  ASSERT_EQ(0u, fc.size());

  db.SetIdentifierTag(*manager, a, 0x0010, 0x0020, "patient");
  db.SetIdentifierTag(*manager, a, 0x0020, 0x000d, "study");
  db.SetMainDicomTag(*manager, a, 0x0010, 0x0020, "patient");
  db.SetMainDicomTag(*manager, a, 0x0020, 0x000d, "study");
  db.SetMainDicomTag(*manager, a, 0x0008, 0x1030, reinterpret_cast<const char*>(UTF8));

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
  db.GetMainDicomTags(*output, *manager, a);
  ASSERT_EQ(3u, countDicomTags);

  db.LookupIdentifier(ci, *manager, OrthancPluginResourceType_Study, 0x0010, 0x0020,
                      OrthancPluginIdentifierConstraint_Equal, "patient");
  ASSERT_EQ(1u, ci.size());
  ASSERT_EQ(a, ci.front());
  db.LookupIdentifier(ci, *manager, OrthancPluginResourceType_Study, 0x0010, 0x0020,
                      OrthancPluginIdentifierConstraint_Equal, "study");
  ASSERT_EQ(0u, ci.size());


  OrthancPluginExportedResource exp;
  exp.seq = -1;
  exp.resourceType = OrthancPluginResourceType_Study;
  exp.publicId = "id";
  exp.modality = "remote";
  exp.date = "date";
  exp.patientId = "patient";
  exp.studyInstanceUid = "study";
  exp.seriesInstanceUid = "series";
  exp.sopInstanceUid = "instance";
  db.LogExportedResource(*manager, exp);

  expectedExported.reset(new OrthancPluginExportedResource());
  *expectedExported = exp;

  bool done;
  db.GetExportedResources(*output, done, *manager, 0, 10);


  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Patient); ASSERT_EQ(0u, pub.size());
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Study); ASSERT_EQ(1u, pub.size());
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Series); ASSERT_EQ(2u, pub.size());
  db.GetAllPublicIds(pub, *manager, OrthancPluginResourceType_Instance); ASSERT_EQ(0u, pub.size());
  ASSERT_EQ(3u, db.GetAllResourcesCount(*manager));

  ASSERT_EQ(0u, db.GetUnprotectedPatientsCount(*manager));  // No patient was inserted
  ASSERT_TRUE(db.IsExistingResource(*manager, c));

  {
    // A transaction is needed here for MySQL, as it was not possible
    // to implement recursive deletion of resources using pure SQL
    // statements
    manager->StartTransaction(TransactionType_ReadWrite);

    deletedAttachments.clear();
    deletedResources.clear();
    remainingAncestor.reset();

    db.DeleteResource(*output, *manager, c);

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

  ASSERT_FALSE(db.IsExistingResource(*manager, c));
  ASSERT_TRUE(db.IsExistingResource(*manager, a));
  ASSERT_TRUE(db.IsExistingResource(*manager, b));
  ASSERT_EQ(2u, db.GetAllResourcesCount(*manager));
  db.DeleteResource(*output, *manager, a);
  ASSERT_EQ(0u, db.GetAllResourcesCount(*manager));
  ASSERT_FALSE(db.IsExistingResource(*manager, a));
  ASSERT_FALSE(db.IsExistingResource(*manager, b));
  ASSERT_FALSE(db.IsExistingResource(*manager, c));

  ASSERT_EQ(0u, deletedAttachments.size());
  ASSERT_EQ(2u, deletedResources.size());
  ASSERT_EQ(OrthancPluginResourceType_Series, deletedResources["series"]);
  ASSERT_EQ(OrthancPluginResourceType_Study, deletedResources["study"]);
  ASSERT_FALSE(remainingAncestor.get() != NULL);

  ASSERT_EQ(0u, db.GetAllResourcesCount(*manager));
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
  db.DeleteResource(*output, *manager, p2);
  ASSERT_TRUE(db.SelectPatientToRecycle(r, *manager, p3));
  ASSERT_EQ(p1, r);

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

    std::string tmp;
    ASSERT_TRUE(db.LookupGlobalProperty(tmp, *manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8));
    ASSERT_EQ(longProperty, tmp);

    tmp.clear();
    ASSERT_TRUE(db.LookupGlobalProperty(tmp, *manager, "some-server", Orthanc::GlobalProperty_DatabaseInternal8));
    ASSERT_EQ(longProperty, tmp);
  }

  db.DeleteResource(*output, *manager, p1);
  db.DeleteResource(*output, *manager, p3);

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

      db.DeleteResource(*output, *manager, resources[level]);

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

      db.DeleteResource(*output, *manager, resources[3]);  // delete instance

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
          throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }

      db.DeleteResource(*output, *manager, resources[0]);
      db.DeleteResource(*output, *manager, unrelated);
    }
  }

  manager->Close();
}
