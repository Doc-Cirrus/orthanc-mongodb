/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
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


/**
 * Counterpart of "PostgreSQL/UnitTests/PostgreSQLTests.cpp", plus one
 * regression test for each defect listed in "PLAN.md".
 *
 * Tests whose name starts with "DISABLED_KnownBug" document a defect
 * that is not fixed yet: they are expected to fail, and are skipped
 * unless "--gtest_also_run_disabled_tests" is given. The phase of
 * "PLAN.md" that fixes the defect removes the "DISABLED_KnownBug_" prefix.
 **/


#include "MongoDBTestsToolbox.h"

#include "../../Framework/MongoDB/MongoDBDatabase.h"
#include "../../Framework/MongoDB/MongoDBParameters.h"
#include "../../Framework/MongoDB/MongoDBToolbox.h"
#include "../../Framework/Plugins/GlobalProperties.h"
#include "../Plugins/MongoDBIndex.h"
#include "../Plugins/MongoDBSchema.h"
#include "../Plugins/MongoDBStorageArea.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <OrthancException.h>
#include <Toolbox.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>


using namespace OrthancDatabases;

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;


namespace
{
  /**
   * Records everything the index sends to Orthanc
   **/
  class RecordingOutput : public IDatabaseBackendOutput
  {
  public:
    std::set<std::string>                            deletedAttachments_;
    std::map<std::string, OrthancPluginResourceType>  deletedResources_;
    std::string                                      remainingAncestor_;
    OrthancPluginResourceType                        remainingAncestorType_;
    std::vector<int64_t>                             changes_;
    std::vector<std::string>                         changesPublicIds_;
    std::set<std::string>                            matches_;
    size_t                                           countMatches_;
    std::map<std::string, std::string>               someInstances_;
    std::map<std::string, std::string>               customData_;  // By attachment UUID

    RecordingOutput() :
      remainingAncestorType_(OrthancPluginResourceType_None),
      countMatches_(0)
    {
    }

    virtual void SignalDeletedAttachment(const std::string& uuid,
                                         int32_t            contentType,
                                         uint64_t           uncompressedSize,
                                         const std::string& uncompressedHash,
                                         int32_t            compressionType,
                                         uint64_t           compressedSize,
                                         const std::string& compressedHash,
                                         const std::string& customData) ORTHANC_OVERRIDE
    {
      deletedAttachments_.insert(uuid);
      customData_[uuid] = customData;
    }

    virtual void SignalDeletedResource(const std::string& publicId,
                                       OrthancPluginResourceType resourceType) ORTHANC_OVERRIDE
    {
      deletedResources_[publicId] = resourceType;
    }

    virtual void SignalRemainingAncestor(const std::string& ancestorId,
                                         OrthancPluginResourceType ancestorType) ORTHANC_OVERRIDE
    {
      remainingAncestor_ = ancestorId;
      remainingAncestorType_ = ancestorType;
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
      customData_[uuid] = customData;
    }

    virtual void AnswerChange(int64_t                    seq,
                              int32_t                    changeType,
                              OrthancPluginResourceType  resourceType,
                              const std::string&         publicId,
                              const std::string&         date) ORTHANC_OVERRIDE
    {
      changes_.push_back(seq);
      changesPublicIds_.push_back(publicId);
    }

    virtual void AnswerDicomTag(uint16_t group,
                                uint16_t element,
                                const std::string& value) ORTHANC_OVERRIDE
    {
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
    }

    virtual void AnswerMatchingResource(const std::string& resourceId) ORTHANC_OVERRIDE
    {
      matches_.insert(resourceId);
      countMatches_++;
    }

    virtual void AnswerMatchingResource(const std::string& resourceId,
                                        const std::string& someInstanceId) ORTHANC_OVERRIDE
    {
      matches_.insert(resourceId);
      countMatches_++;
      someInstances_[resourceId] = someInstanceId;
    }
  };


  /**
   * An index connected to its own, empty test database
   **/
  class MongoDBIndexTest : public ::testing::Test
  {
  protected:
    std::unique_ptr<TestDatabase>     database_;
    std::unique_ptr<MongoDBIndex>     index_;
    std::unique_ptr<DatabaseManager>  manager_;

    virtual void SetUp() ORTHANC_OVERRIDE
    {
      database_.reset(new TestDatabase);
      index_.reset(new MongoDBIndex(NULL, CreateTestParameters(database_->GetUri()), false));

      std::list<IdentifierTag> identifierTags;
      manager_.reset(IndexBackend::CreateSingleDatabaseManager(*index_, false, identifierTags));
    }

    virtual void TearDown() ORTHANC_OVERRIDE
    {
      manager_.reset();
      index_.reset();
      database_.reset();  // Drops the database
    }

    // Starts another plugin on the same database, as another Orthanc would
    DatabaseManager* StartAnotherIndex(std::unique_ptr<MongoDBIndex>& index)
    {
      return StartAnotherIndex(index, CreateTestParameters(database_->GetUri()));
    }

    DatabaseManager* StartAnotherIndex(std::unique_ptr<MongoDBIndex>& index,
                                       const MongoDBParameters& parameters)
    {
      index.reset(new MongoDBIndex(NULL, parameters, false));

      std::list<IdentifierTag> identifierTags;
      return IndexBackend::CreateSingleDatabaseManager(*index, false, identifierTags);
    }

    std::string GetGlobalProperty(Orthanc::GlobalProperty property)
    {
      std::string value;
      return (index_->LookupGlobalProperty(value, *manager_, MISSING_SERVER_IDENTIFIER, property) ? value : "");
    }

    // Direct access to the collections, bypassing the plugin
    mongocxx::collection GetCollection(mongocxx::client& client,
                                       const std::string& name)
    {
      return client[database_->GetName()][name];
    }

    int64_t CountDocuments(const std::string& collection,
                           const bsoncxx::document::view_or_value& filter = make_document())
    {
      mongocxx::client client{mongocxx::uri{database_->GetUri()}};
      return GetCollection(client, collection).count_documents(filter);
    }

    // patient -> study -> series -> instance, returns the internal IDs
    std::vector<int64_t> CreateHierarchy(const std::string& prefix)
    {
      std::vector<int64_t> ids;
      ids.push_back(index_->CreateResource(*manager_, (prefix + "patient").c_str(), OrthancPluginResourceType_Patient));
      ids.push_back(index_->CreateResource(*manager_, (prefix + "study").c_str(), OrthancPluginResourceType_Study));
      ids.push_back(index_->CreateResource(*manager_, (prefix + "series").c_str(), OrthancPluginResourceType_Series));
      ids.push_back(index_->CreateResource(*manager_, (prefix + "instance").c_str(), OrthancPluginResourceType_Instance));
      index_->AttachChild(*manager_, ids[0], ids[1]);
      index_->AttachChild(*manager_, ids[1], ids[2]);
      index_->AttachChild(*manager_, ids[2], ids[3]);
      return ids;
    }

    void AddAttachment(int64_t id,
                       const std::string& uuid)
    {
      OrthancPluginAttachment attachment;
      attachment.uuid = uuid.c_str();
      attachment.contentType = OrthancPluginContentType_Dicom;
      attachment.uncompressedSize = 42;
      attachment.uncompressedHash = "md5";
      attachment.compressionType = OrthancPluginCompressionType_None;
      attachment.compressedSize = 42;
      attachment.compressedHash = "md5";
      index_->AddAttachment(*manager_, id, attachment, 0);
    }

    void Lookup(RecordingOutput& output,
                OrthancPluginResourceType level,
                DatabaseConstraints& constraints,
                uint32_t limit = 0,
                bool requestSomeInstance = false)
    {
      std::set<std::string> noLabels;
      index_->LookupResources(output, *manager_, constraints, level, noLabels, LabelsConstraint_All,
                              limit, requestSomeInstance);
    }

    // Creates a resource, attached to "parent" if it is not -1
    int64_t CreateChild(const std::string& publicId,
                        OrthancPluginResourceType type,
                        int64_t parent)
    {
      int64_t id = index_->CreateResource(*manager_, publicId.c_str(), type);
      if (parent != -1)
      {
        index_->AttachChild(*manager_, parent, id);
      }
      return id;
    }

    std::set<std::string> LookupMatches(OrthancPluginResourceType level,
                                        DatabaseConstraints& constraints)
    {
      RecordingOutput output;
      Lookup(output, level, constraints);
      EXPECT_EQ(output.matches_.size(), output.countMatches_);  // No duplicate answer
      return output.matches_;
    }
  };


  DatabaseConstraint* CreateConstraint(Orthanc::ResourceType level,
                                       uint16_t group,
                                       uint16_t element,
                                       bool isIdentifier,
                                       ConstraintType type,
                                       const std::string& value,
                                       bool caseSensitive,
                                       bool mandatory = true)
  {
    std::vector<std::string> values;
    values.push_back(value);
    return new DatabaseConstraint(level, Orthanc::DicomTag(group, element), isIdentifier,
                                  type, values, caseSensitive, mandatory);
  }


  DatabaseConstraint* CreateListConstraint(Orthanc::ResourceType level,
                                           uint16_t group,
                                           uint16_t element,
                                           const std::vector<std::string>& values,
                                           bool caseSensitive)
  {
    return new DatabaseConstraint(level, Orthanc::DicomTag(group, element), false /* identifier */,
                                  ConstraintType_List, values, caseSensitive, true /* mandatory */);
  }


  std::set<std::string> MakeSet(const char* a = NULL,
                                const char* b = NULL,
                                const char* c = NULL)
  {
    std::set<std::string> s;
    if (a != NULL) s.insert(a);
    if (b != NULL) s.insert(b);
    if (c != NULL) s.insert(c);
    return s;
  }


  /**
   * Fake Orthanc core for the storage area: it records the callbacks
   * that the plugin registers, and allocates the memory buffers
   **/
  struct StorageCallbacks
  {
    OrthancPluginStorageCreate     create_;
    OrthancPluginStorageReadWhole  readWhole_;
    OrthancPluginStorageReadRange  readRange_;
    OrthancPluginStorageRemove     remove_;
  };

  StorageCallbacks storageCallbacks_;


  OrthancPluginErrorCode InvokeStorageService(struct _OrthancPluginContext_t* context,
                                              _OrthancPluginService service,
                                              const void* params)
  {
    switch (service)
    {
      case _OrthancPluginService_RegisterStorageArea2:
      {
        const _OrthancPluginRegisterStorageArea2& p =
          *reinterpret_cast<const _OrthancPluginRegisterStorageArea2*>(params);
        storageCallbacks_.create_ = p.create;
        storageCallbacks_.readWhole_ = p.readWhole;
        storageCallbacks_.readRange_ = p.readRange;
        storageCallbacks_.remove_ = p.remove;
        return OrthancPluginErrorCode_Success;
      }

      case _OrthancPluginService_CreateMemoryBuffer64:
      {
        const _OrthancPluginCreateMemoryBuffer64& p =
          *reinterpret_cast<const _OrthancPluginCreateMemoryBuffer64*>(params);
        p.target->size = p.size;
        p.target->data = (p.size == 0 ? NULL : malloc(p.size));
        return (p.size != 0 && p.target->data == NULL) ?
          OrthancPluginErrorCode_NotEnoughMemory : OrthancPluginErrorCode_Success;
      }

      case _OrthancPluginService_LogError:
      case _OrthancPluginService_LogWarning:
      case _OrthancPluginService_LogInfo:
        printf("%s\n", reinterpret_cast<const char*>(params));
        return OrthancPluginErrorCode_Success;

      default:
        printf("Service not emulated by the storage tests: %d\n", service);
        return OrthancPluginErrorCode_NotImplemented;
    }
  }


  /**
   * "StorageBackend::Register()" can only be called once per
   * process, so all the storage tests share one database
   **/
  class MongoDBStorageTest : public ::testing::Test
  {
  protected:
    static std::unique_ptr<TestDatabase>  database_;
    static OrthancPluginContext           context_;

    static void SetUpTestCase()
    {
      database_.reset(new TestDatabase);

      context_.pluginsManager = NULL;
      context_.orthancVersion = "mainline";
      context_.Free = ::free;
      context_.InvokeService = InvokeStorageService;

      memset(&storageCallbacks_, 0, sizeof(storageCallbacks_));
      StorageBackend::Register(&context_, new MongoDBStorageArea(CreateTestParameters(database_->GetUri())));
    }

    static void TearDownTestCase()
    {
      StorageBackend::Finalize();
      database_.reset();
    }

    int64_t CountFiles()
    {
      mongocxx::client client{mongocxx::uri{database_->GetUri()}};
      return client[database_->GetName()]["fs.files"].count_documents({});
    }

    int64_t CountChunks()
    {
      mongocxx::client client{mongocxx::uri{database_->GetUri()}};
      return client[database_->GetName()]["fs.chunks"].count_documents({});
    }

    static OrthancPluginErrorCode Create(const std::string& uuid,
                                         const std::string& content)
    {
      return storageCallbacks_.create_(uuid.c_str(), content.c_str(), content.size(), OrthancPluginContentType_Unknown);
    }

    static OrthancPluginErrorCode ReadWhole(std::string& target,
                                            const std::string& uuid)
    {
      OrthancPluginMemoryBuffer64 buffer;
      buffer.data = NULL;
      buffer.size = 0;

      OrthancPluginErrorCode code = storageCallbacks_.readWhole_(&buffer, uuid.c_str(), OrthancPluginContentType_Unknown);
      if (code == OrthancPluginErrorCode_Success)
      {
        target.assign(reinterpret_cast<const char*>(buffer.data), buffer.size);
      }

      free(buffer.data);
      return code;
    }

    // Like Orthanc, allocate the buffer before reading the range
    static OrthancPluginErrorCode ReadRange(std::string& target,
                                            const std::string& uuid,
                                            uint64_t start,
                                            uint64_t size)
    {
      OrthancPluginMemoryBuffer64 buffer;
      buffer.size = size;
      buffer.data = malloc(size == 0 ? 1 : size);

      OrthancPluginErrorCode code = storageCallbacks_.readRange_(&buffer, uuid.c_str(), OrthancPluginContentType_Unknown, start);
      if (code == OrthancPluginErrorCode_Success)
      {
        target.assign(reinterpret_cast<const char*>(buffer.data), buffer.size);
      }

      free(buffer.data);
      return code;
    }

    static OrthancPluginErrorCode Remove(const std::string& uuid)
    {
      return storageCallbacks_.remove_(uuid.c_str(), OrthancPluginContentType_Unknown);
    }
  };

  std::unique_ptr<TestDatabase>  MongoDBStorageTest::database_;
  OrthancPluginContext           MongoDBStorageTest::context_;


  // Runs "f(thread)" on "count" threads that start at the same time
  template <typename F>
  void RunConcurrently(size_t count,
                       F f)
  {
    std::atomic<size_t> ready(0);
    std::atomic<size_t> failures(0);
    std::vector<std::thread> threads;

    for (size_t i = 0; i < count; i++)
    {
      threads.push_back(std::thread([&, i]()
      {
        ready++;
        while (ready < count)
        {
          std::this_thread::yield();
        }

        try
        {
          f(i);
        }
        catch (Orthanc::OrthancException& e)
        {
          printf("Thread %d: %s\n", static_cast<int>(i), e.What());
          failures++;
        }
        catch (std::exception& e)
        {
          printf("Thread %d: %s\n", static_cast<int>(i), e.what());
          failures++;
        }
      }));
    }

    for (size_t i = 0; i < threads.size(); i++)
    {
      threads[i].join();
    }

    ASSERT_EQ(0u, failures.load());
  }


  /**
   * Runs "f()" in a read-write transaction, and retries it on
   * "DatabaseCannotSerialize", as Orthanc does in
   * "StatelessDatabaseOperations::ApplyInternal()"
   **/
  template <typename F>
  void ApplyWithRetries(DatabaseManager& manager,
                        F f)
  {
    static const unsigned int MAX_RETRIES = 10;

    for (unsigned int attempt = 0; ; attempt++)
    {
      try
      {
        DatabaseManager::Transaction transaction(manager, TransactionType_ReadWrite);
        f();
        transaction.Commit();
        return;
      }
      catch (Orthanc::OrthancException& e)
      {
        if (e.GetErrorCode() != Orthanc::ErrorCode_DatabaseCannotSerialize ||
            attempt >= MAX_RETRIES)
        {
          throw;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1) + 5 * (rand() % 10)));
      }
    }
  }
}


TEST(MongoDBParameters, Basic)
{
  MongoDBParameters p;
  ASSERT_TRUE(p.GetConnectionUri().empty());
  ASSERT_EQ(261120u, p.GetChunkSize());
  ASSERT_EQ(10u, p.GetMaxConnectionRetries());
  ASSERT_EQ(5u, p.GetConnectionRetryInterval());
  ASSERT_EQ(MongoDBTransactionsMode_Auto, p.GetTransactionsMode());
  ASSERT_TRUE(p.IsCreateIndexesAtStartup());
  ASSERT_TRUE(p.IsExtendedFindEnabled());

  p.SetConnectionUri("mongodb://localhost:27017/orthanc");
  ASSERT_EQ("orthanc", p.GetDatabaseName());

  p.SetConnectionUri("mongodb://user:pass@host1:27018,host2:27019/hello?replicaSet=rs0");
  ASSERT_EQ("hello", p.GetDatabaseName());

  // The database is mandatory
  ASSERT_THROW(p.SetConnectionUri("mongodb://localhost:27017/"), Orthanc::OrthancException);
  ASSERT_THROW(p.SetConnectionUri("mongodb://localhost:27017"), Orthanc::OrthancException);
  ASSERT_THROW(p.SetConnectionUri("postgresql://localhost/orthanc"), Orthanc::OrthancException);
  ASSERT_EQ("hello", p.GetDatabaseName());  // Unchanged

  ASSERT_THROW(p.SetChunkSize(0), Orthanc::OrthancException);
  ASSERT_THROW(p.SetConnectionRetryInterval(0), Orthanc::OrthancException);

  ASSERT_EQ(MongoDBTransactionsMode_Auto, MongoDBParameters::ParseTransactionsMode("Auto"));
  ASSERT_EQ(MongoDBTransactionsMode_Auto, MongoDBParameters::ParseTransactionsMode("auto"));
  ASSERT_EQ(MongoDBTransactionsMode_Enabled, MongoDBParameters::ParseTransactionsMode(true));
  ASSERT_EQ(MongoDBTransactionsMode_Disabled, MongoDBParameters::ParseTransactionsMode(false));
  ASSERT_EQ(MongoDBTransactionsMode_Enabled, MongoDBParameters::ParseTransactionsMode("true"));
  ASSERT_THROW(MongoDBParameters::ParseTransactionsMode("nope"), Orthanc::OrthancException);
  ASSERT_THROW(MongoDBParameters::ParseTransactionsMode(42), Orthanc::OrthancException);
}


namespace
{
  // Parameters of the plugin, read from the JSON of a "MongoDB" section
  MongoDBParameters ParseParameters(const std::string& json)
  {
    Json::Value section;
    if (!Orthanc::Toolbox::ReadJson(section, json))
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat, json);
    }

    return MongoDBParameters(OrthancPlugins::OrthancConfiguration(section, "MongoDB"));
  }


  // Message of the exception thrown by reading the given "MongoDB" section, empty if none
  std::string GetParametersError(const std::string& json)
  {
    try
    {
      ParseParameters(json);
      return "";
    }
    catch (Orthanc::OrthancException& e)
    {
      EXPECT_EQ(Orthanc::ErrorCode_ParameterOutOfRange, e.GetErrorCode());
      return e.GetDetails() == NULL ? "(no details)" : e.GetDetails();
    }
  }
}


TEST(MongoDBParameters, ConnectionOptions)
{
  // The options of the releases <= 1.9.1, with their defaults "localhost" and 27017
  ASSERT_EQ("mongodb://localhost:27017/orthanc", ParseParameters("{ \"database\" : \"orthanc\" }").GetConnectionUri());
  ASSERT_EQ("orthanc", ParseParameters("{ \"database\" : \"orthanc\" }").GetDatabaseName());

  ASSERT_EQ("mongodb://user:password@customhost:27001/database?authSource=admin",
            ParseParameters("{ \"host\" : \"customhost\", \"port\" : 27001, \"user\" : \"user\", "
                            "\"database\" : \"database\", \"password\" : \"password\", "
                            "\"authenticationDatabase\" : \"admin\", \"ChunkSize\" : 261120 }").GetConnectionUri());

  // The user, the password and the authentication database are percent-encoded
  ASSERT_EQ("mongodb://a%40b:p%40ss%3Aw%2Frd%25@db1:27017/orthanc?authSource=my%20admin",
            ParseParameters("{ \"host\" : \"db1\", \"user\" : \"a@b\", \"password\" : \"p@ss:w/rd%\", "
                            "\"database\" : \"orthanc\", \"authenticationDatabase\" : \"my admin\" }").GetConnectionUri());

  // A user without password (e.g. X.509), an authentication database without user
  ASSERT_EQ("mongodb://user@localhost:27017/orthanc",
            ParseParameters("{ \"user\" : \"user\", \"database\" : \"orthanc\" }").GetConnectionUri());
  ASSERT_EQ("mongodb://localhost:27017/orthanc?authSource=admin",
            ParseParameters("{ \"authenticationDatabase\" : \"admin\", \"database\" : \"orthanc\" }").GetConnectionUri());

  // Empty values count as absent, e.g. "${MONGODB_USER}" with no such environment variable
  ASSERT_EQ("mongodb://localhost:27017/orthanc",
            ParseParameters("{ \"user\" : \"\", \"password\" : \"\", \"authenticationDatabase\" : \"\", "
                            "\"database\" : \"orthanc\" }").GetConnectionUri());

  // IPv6 addresses are put in brackets
  ASSERT_EQ("mongodb://[::1]:27017/orthanc",
            ParseParameters("{ \"host\" : \"::1\", \"database\" : \"orthanc\" }").GetConnectionUri());

  // "ConnectionUri" overrides the separate options (as in the releases <= 1.9.1)
  ASSERT_EQ("mongodb://other:1234/uri",
            ParseParameters("{ \"ConnectionUri\" : \"mongodb://other:1234/uri\", \"host\" : \"customhost\", "
                            "\"database\" : \"database\" }").GetConnectionUri());
  ASSERT_EQ("mongodb://localhost:27017/orthanc",
            ParseParameters("{ \"ConnectionUri\" : \"\", \"database\" : \"orthanc\" }").GetConnectionUri());

  // The other options are read in both cases
  ASSERT_EQ(1024u, ParseParameters("{ \"database\" : \"orthanc\", \"ChunkSize\" : 1024 }").GetChunkSize());

  // Invalid options: the error names the option, and never contains the password
  const std::string password = "\"password\" : \"s3cr3t-p@ss\"";
  ASSERT_NE(std::string::npos, GetParametersError("{}").find("ConnectionUri"));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"host\" : \"db1\" }").find("\"database\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"database\" : \"\" }").find("\"database\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"host\" : \"\", \"database\" : \"orthanc\" }").find("\"host\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"host\" : \"h1,h2\", \"database\" : \"orthanc\" }").find("\"host\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"host\" : \"h1:27001\", \"database\" : \"orthanc\" }").find("\"port\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"port\" : 0, \"database\" : \"orthanc\" }").find("\"port\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"port\" : 65536, \"database\" : \"orthanc\" }").find("\"port\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"database\" : \"a.b\" }").find("\"database\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"database\" : \"a/b\" }").find("\"database\""));
  ASSERT_NE(std::string::npos, GetParametersError("{ \"database\" : \"" + std::string(64, 'a') + "\" }").find("\"database\""));

  const std::string noUser = GetParametersError("{ \"database\" : \"orthanc\", " + password + " }");
  ASSERT_NE(std::string::npos, noUser.find("\"user\""));
  ASSERT_EQ(std::string::npos, noUser.find("s3cr3t"));

  const std::string badHost = GetParametersError("{ \"host\" : \"h1 h2\", \"user\" : \"u\", \"database\" : \"orthanc\", " + password + " }");
  ASSERT_NE(std::string::npos, badHost.find("\"host\""));
  ASSERT_EQ(std::string::npos, badHost.find("s3cr3t"));

  // A host that the driver rejects: the error is ours, not the one of the driver, which shows the URI
  const std::string rejected = GetParametersError("{ \"host\" : \"bad%host\", \"user\" : \"u\", \"database\" : \"orthanc\", " + password + " }");
  ASSERT_NE(std::string::npos, rejected.find("\"host\""));
  ASSERT_EQ(std::string::npos, rejected.find("s3cr3t"));

  // A wrong type is a "BadFileFormat" error of the configuration
  ASSERT_THROW(ParseParameters("{ \"port\" : \"27017\", \"database\" : \"orthanc\" }"), Orthanc::OrthancException);
  ASSERT_THROW(ParseParameters("{ \"database\" : 42 }"), Orthanc::OrthancException);
}


TEST(MongoDBTestsToolbox, SetDatabaseInUri)
{
  ASSERT_EQ("mongodb://localhost:27017/db", SetDatabaseInUri("mongodb://localhost:27017", "db"));
  ASSERT_EQ("mongodb://localhost:27017/db", SetDatabaseInUri("mongodb://localhost:27017/", "db"));
  ASSERT_EQ("mongodb://localhost:27017/db", SetDatabaseInUri("mongodb://localhost:27017/orthanc", "db"));
  ASSERT_EQ("mongodb://h1,h2/db?replicaSet=rs0", SetDatabaseInUri("mongodb://h1,h2/?replicaSet=rs0", "db"));
  ASSERT_EQ("mongodb://h1/db?replicaSet=rs0", SetDatabaseInUri("mongodb://h1?replicaSet=rs0", "db"));
  ASSERT_EQ("mongodb://u:p@h1/db?a=1", SetDatabaseInUri("mongodb://u:p@h1/orthanc?a=1", "db"));
}


TEST(MongoDBDatabase, TransactionsMode)
{
  TestDatabase database;
  MongoDBParameters parameters = CreateTestParameters(database.GetUri());
  const bool replicaSet = IsTestServerReplicaSet();

  {
    std::unique_ptr<MongoDBDatabase> db(MongoDBDatabase::CreateDatabaseConnection(parameters));
    ASSERT_EQ(replicaSet, db->HasTransactions());
  }

  {
    parameters.SetTransactionsMode(MongoDBTransactionsMode_Disabled);
    std::unique_ptr<MongoDBDatabase> db(MongoDBDatabase::CreateDatabaseConnection(parameters));
    ASSERT_FALSE(db->HasTransactions());
  }

  parameters.SetTransactionsMode(MongoDBTransactionsMode_Enabled);
  if (replicaSet)
  {
    std::unique_ptr<MongoDBDatabase> db(MongoDBDatabase::CreateDatabaseConnection(parameters));
    ASSERT_TRUE(db->HasTransactions());
  }
  else
  {
    // Requiring transactions from a standalone server fails at startup
    ASSERT_THROW(MongoDBDatabase::CreateDatabaseConnection(parameters), Orthanc::OrthancException);
  }
}


TEST(MongoDBDatabase, CheckServerVersion)
{
  // MongoDB 7.0 is wire version 21
  ASSERT_NO_THROW(MongoDBDatabase::CheckServerVersion(make_document(kvp("maxWireVersion", 21))));
  ASSERT_NO_THROW(MongoDBDatabase::CheckServerVersion(make_document(kvp("maxWireVersion", 25))));

  try
  {
    MongoDBDatabase::CheckServerVersion(make_document(kvp("maxWireVersion", 17)));  // MongoDB 6.0
    FAIL();
  }
  catch (Orthanc::OrthancException& e)
  {
    ASSERT_EQ(Orthanc::ErrorCode_IncompatibleDatabaseVersion, e.GetErrorCode());
  }

  ASSERT_THROW(MongoDBDatabase::CheckServerVersion(make_document()), Orthanc::OrthancException);
}


TEST(MongoDBDatabase, ConvertErrors)
{
  TestDatabase database;
  std::unique_ptr<MongoDBDatabase> db(MongoDBDatabase::CreateDatabaseConnection(CreateTestParameters(database.GetUri())));

  mongocxx::options::index unique;
  unique.unique(true);
  db->GetDatabase()["Unique"].create_index(make_document(kvp("key", 1)), unique);

  MongoDBCollection collection = db->GetCollection("Unique");
  collection.InsertOne(make_document(kvp("key", 1)));

  // A duplicate key makes Orthanc retry the whole transaction
  try
  {
    collection.InsertOne(make_document(kvp("key", 1)));
    FAIL();
  }
  catch (Orthanc::OrthancException& e)
  {
    ASSERT_EQ(Orthanc::ErrorCode_DatabaseCannotSerialize, e.GetErrorCode());
  }

  // Same for the bulk writes
  try
  {
    MongoDBCollection::Documents documents;
    documents.push_back(make_document(kvp("key", 2)));
    documents.push_back(make_document(kvp("key", 2)));
    collection.InsertMany(documents);
    FAIL();
  }
  catch (Orthanc::OrthancException& e)
  {
    ASSERT_EQ(Orthanc::ErrorCode_DatabaseCannotSerialize, e.GetErrorCode());
  }

  // Other errors are plain database errors
  try
  {
    collection.UpdateOne(make_document(), make_document(kvp("$unknownOperator", make_document(kvp("a", 1)))));
    FAIL();
  }
  catch (Orthanc::OrthancException& e)
  {
    ASSERT_EQ(Orthanc::ErrorCode_Database, e.GetErrorCode());
  }

  // The sequences start at 1, and are not rolled back by transactions
  ASSERT_EQ(1, db->GetNextSequence("Test"));
  ASSERT_EQ(2, db->GetNextSequence("Test"));
}


TEST(MongoDBDatabase, Unavailable)
{
  // Nothing listens on port 1: the connection fails without retrying
  MongoDBParameters parameters = CreateTestParameters("mongodb://localhost:1/test?serverSelectionTimeoutMS=200");

  try
  {
    std::unique_ptr<MongoDBDatabase> db(MongoDBDatabase::CreateDatabaseConnection(parameters));
    FAIL();
  }
  catch (Orthanc::OrthancException& e)
  {
    ASSERT_EQ(Orthanc::ErrorCode_DatabaseUnavailable, e.GetErrorCode());
  }
}


TEST(MongoDBDatabase, ConnectionOptions)
{
  TestDatabase database;

  const mongocxx::uri testUri{GetTestConnectionUri()};
  ASSERT_FALSE(testUri.hosts().empty());

  // A user of the test database, whose password needs percent-encoding. The
  // server checks the credentials even if it does not enforce authentication.
  const std::string user = "user_" + database.GetName();
  const std::string password = "p@ss:w/rd%";

  mongocxx::client client{mongocxx::uri{database.GetUri()}};
  mongocxx::database admin = client[database.GetName()];
  admin.run_command(make_document(
                      kvp("createUser", user),
                      kvp("pwd", password),
                      kvp("roles", bsoncxx::builder::basic::make_array(
                            make_document(kvp("role", "readWrite"), kvp("db", database.GetName()))))));

  struct DropUsers
  {
    mongocxx::database& database_;

    ~DropUsers()
    {
      database_.run_command(make_document(kvp("dropAllUsersFromDatabase", 1)));
    }
  } dropUsers = { admin };

  // The host of the test server, without the options of its URI (e.g. "replicaSet")
  Json::Value section;
  section["host"] = std::string(testUri.hosts()[0].name);
  section["port"] = testUri.hosts()[0].port;
  section["database"] = database.GetName();

  {
    // No credentials
    MongoDBParameters parameters{OrthancPlugins::OrthancConfiguration(section, "MongoDB")};
    parameters.SetMaxConnectionRetries(0);

    std::unique_ptr<MongoDBDatabase> db(MongoDBDatabase::CreateDatabaseConnection(parameters));

    // The replica set is detected by "hello", even without "replicaSet" in the URI
    ASSERT_EQ(IsTestServerReplicaSet(), db->HasTransactions());
    db->GetCollection("Options").InsertOne(make_document(kvp("credentials", false)));
  }

  section["user"] = user;
  section["password"] = password;
  section["authenticationDatabase"] = database.GetName();

  {
    MongoDBParameters parameters{OrthancPlugins::OrthancConfiguration(section, "MongoDB")};
    parameters.SetMaxConnectionRetries(0);

    std::unique_ptr<MongoDBDatabase> db(MongoDBDatabase::CreateDatabaseConnection(parameters));
    db->GetCollection("Options").InsertOne(make_document(kvp("credentials", true)));
  }

  ASSERT_EQ(2, client[database.GetName()]["Options"].count_documents(make_document()));

  {
    // A wrong password fails, without showing the password
    section["password"] = "wrong-s3cr3t";
    MongoDBParameters parameters{OrthancPlugins::OrthancConfiguration(section, "MongoDB")};
    parameters.SetMaxConnectionRetries(0);

    try
    {
      std::unique_ptr<MongoDBDatabase> db(MongoDBDatabase::CreateDatabaseConnection(parameters));
      db->GetCollection("Options").InsertOne(make_document(kvp("credentials", true)));
      FAIL();
    }
    catch (Orthanc::OrthancException& e)
    {
      const std::string message = std::string(e.What()) + " " + (e.GetDetails() == NULL ? "" : e.GetDetails());
      ASSERT_EQ(std::string::npos, message.find("s3cr3t"));
    }
  }
}


TEST_F(MongoDBIndexTest, CreateInstance)
{
  OrthancPluginCreateInstanceResult r1, r2;

  memset(&r1, 0, sizeof(r1));
  index_->CreateInstance(r1, *manager_, "a", "b", "c", "d");
  ASSERT_TRUE(r1.isNewInstance);
  ASSERT_TRUE(r1.isNewSeries);
  ASSERT_TRUE(r1.isNewStudy);
  ASSERT_TRUE(r1.isNewPatient);

  memset(&r2, 0, sizeof(r2));
  index_->CreateInstance(r2, *manager_, "a", "b", "c", "d");
  ASSERT_FALSE(r2.isNewInstance);
  ASSERT_EQ(r1.instanceId, r2.instanceId);

  memset(&r2, 0, sizeof(r2));
  index_->CreateInstance(r2, *manager_, "a", "b", "c", "e");
  ASSERT_TRUE(r2.isNewInstance);
  ASSERT_FALSE(r2.isNewSeries);
  ASSERT_FALSE(r2.isNewStudy);
  ASSERT_FALSE(r2.isNewPatient);
  ASSERT_EQ(r1.patientId, r2.patientId);
  ASSERT_EQ(r1.studyId, r2.studyId);
  ASSERT_EQ(r1.seriesId, r2.seriesId);
  ASSERT_NE(r1.instanceId, r2.instanceId);

  memset(&r2, 0, sizeof(r2));
  index_->CreateInstance(r2, *manager_, "a", "b", "f", "g");
  ASSERT_TRUE(r2.isNewInstance);
  ASSERT_TRUE(r2.isNewSeries);
  ASSERT_FALSE(r2.isNewStudy);
  ASSERT_FALSE(r2.isNewPatient);
  ASSERT_EQ(r1.patientId, r2.patientId);
  ASSERT_EQ(r1.studyId, r2.studyId);
  ASSERT_NE(r1.seriesId, r2.seriesId);
  ASSERT_NE(r1.instanceId, r2.instanceId);

  memset(&r2, 0, sizeof(r2));
  index_->CreateInstance(r2, *manager_, "a", "h", "i", "j");
  ASSERT_TRUE(r2.isNewInstance);
  ASSERT_TRUE(r2.isNewSeries);
  ASSERT_TRUE(r2.isNewStudy);
  ASSERT_FALSE(r2.isNewPatient);
  ASSERT_EQ(r1.patientId, r2.patientId);
  ASSERT_NE(r1.studyId, r2.studyId);
  ASSERT_NE(r1.seriesId, r2.seriesId);
  ASSERT_NE(r1.instanceId, r2.instanceId);

  memset(&r2, 0, sizeof(r2));
  index_->CreateInstance(r2, *manager_, "k", "l", "m", "n");
  ASSERT_TRUE(r2.isNewInstance);
  ASSERT_TRUE(r2.isNewSeries);
  ASSERT_TRUE(r2.isNewStudy);
  ASSERT_TRUE(r2.isNewPatient);
  ASSERT_NE(r1.patientId, r2.patientId);
  ASSERT_NE(r1.studyId, r2.studyId);
  ASSERT_NE(r1.seriesId, r2.seriesId);
  ASSERT_NE(r1.instanceId, r2.instanceId);

  ASSERT_EQ(14u, index_->GetAllResourcesCount(*manager_));
  ASSERT_EQ(2u, index_->GetUnprotectedPatientsCount(*manager_));

  // "CreateInstance()" and "CreateResource()" + "AttachChild()" must
  // produce the same hierarchy
  std::string s;
  ASSERT_TRUE(index_->GetParentPublicId(s, *manager_, r1.instanceId));  ASSERT_EQ("c", s);
  ASSERT_TRUE(index_->GetParentPublicId(s, *manager_, r1.seriesId));  ASSERT_EQ("b", s);
  ASSERT_TRUE(index_->GetParentPublicId(s, *manager_, r1.studyId));  ASSERT_EQ("a", s);
  ASSERT_FALSE(index_->GetParentPublicId(s, *manager_, r1.patientId));
}


TEST_F(MongoDBIndexTest, AttachChildLevels)
{
  // The fields "0" to "3" of "Resources" must be the same, whether
  // the hierarchy was created by "CreateInstance()" or by
  // "CreateResource()" and "AttachChild()"
  OrthancPluginCreateInstanceResult r;
  memset(&r, 0, sizeof(r));
  index_->CreateInstance(r, *manager_, "p1", "st1", "se1", "i1");

  std::vector<int64_t> ids = CreateHierarchy("x");

  mongocxx::client client{mongocxx::uri{database_->GetUri()}};
  auto resources = GetCollection(client, "Resources");

  const int64_t viaCreateInstance[4] = { r.patientId, r.studyId, r.seriesId, r.instanceId };

  for (int level = 0; level < 4; level++)
  {
    auto a = resources.find_one(make_document(kvp("internalId", viaCreateInstance[level])));
    auto b = resources.find_one(make_document(kvp("internalId", ids[level])));
    ASSERT_TRUE(a && b);

    for (int field = 0; field < 4; field++)
    {
      const std::string key = std::to_string(field);
      auto arrayA = a->view()[key].get_array().value;
      auto arrayB = b->view()[key].get_array().value;
      ASSERT_EQ(std::distance(arrayA.begin(), arrayA.end()), std::distance(arrayB.begin(), arrayB.end()));
      ASSERT_EQ(1, std::distance(arrayB.begin(), arrayB.end()));
      ASSERT_EQ(ids[field], arrayB.begin()->get_int64().value);
    }
  }
}


TEST_F(MongoDBIndexTest, LastChangeIndex)
{
  ASSERT_EQ(0, index_->GetLastChangeIndex(*manager_));

  std::vector<int64_t> ids = CreateHierarchy("");
  index_->LogChange(*manager_, 1, ids[0], OrthancPluginResourceType_Patient, "20260923T000000");
  index_->LogChange(*manager_, 2, ids[1], OrthancPluginResourceType_Study, "20260923T000001");
  index_->LogChange(*manager_, 3, ids[3], OrthancPluginResourceType_Instance, "20260923T000002");
  ASSERT_EQ(3, index_->GetLastChangeIndex(*manager_));

  RecordingOutput output;
  bool done;
  index_->GetChanges(output, done, *manager_, 0, 2);
  ASSERT_FALSE(done);
  ASSERT_EQ(2u, output.changes_.size());
  ASSERT_EQ(1, output.changes_[0]);
  ASSERT_EQ(2, output.changes_[1]);
  ASSERT_EQ("patient", output.changesPublicIds_[0]);
  ASSERT_EQ("study", output.changesPublicIds_[1]);

  // Deleting the resources deletes their changes, but not the index
  index_->DeleteResource(output, *manager_, ids[0]);
  ASSERT_EQ(0, CountDocuments("Changes"));
  ASSERT_EQ(3, index_->GetLastChangeIndex(*manager_));
}


TEST_F(MongoDBIndexTest, Transaction)
{
  if (!IsTestServerReplicaSet())
  {
    printf("Skipped: multi-document transactions need a replica set\n");
    return;
  }

  manager_->StartTransaction(TransactionType_ReadWrite);
  index_->CreateResource(*manager_, "committed", OrthancPluginResourceType_Patient);
  manager_->CommitTransaction();

  ASSERT_EQ(1u, index_->GetAllResourcesCount(*manager_));
}


// Phase 4: "MongoDBTransaction" maps Orthanc transactions to sessions
TEST_F(MongoDBIndexTest, TransactionRollback)
{
  if (!IsTestServerReplicaSet())
  {
    printf("Skipped: multi-document transactions need a replica set\n");
    return;
  }

  manager_->StartTransaction(TransactionType_ReadWrite);
  index_->CreateResource(*manager_, "rolledback", OrthancPluginResourceType_Patient);
  manager_->RollbackTransaction();

  int64_t id;
  OrthancPluginResourceType type;
  ASSERT_FALSE(index_->LookupResource(id, type, *manager_, "rolledback"));
  ASSERT_EQ(0u, index_->GetAllResourcesCount(*manager_));
}


TEST_F(MongoDBStorageTest, StorageArea)
{
  ASSERT_TRUE(storageCallbacks_.create_ != NULL);
  ASSERT_TRUE(storageCallbacks_.readWhole_ != NULL);
  ASSERT_TRUE(storageCallbacks_.readRange_ != NULL);
  ASSERT_TRUE(storageCallbacks_.remove_ != NULL);

  ASSERT_EQ(0, CountFiles());

  for (int i = 0; i < 10; i++)
  {
    std::string uuid = "uuid-" + std::to_string(i);
    std::string value = "Value " + std::to_string(i * 2);
    ASSERT_EQ(OrthancPluginErrorCode_Success, Create(uuid, value));
  }

  std::string buffer;
  ASSERT_NE(OrthancPluginErrorCode_Success, ReadWhole(buffer, "nope"));

  ASSERT_EQ(10, CountFiles());
  ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("uuid-5"));
  ASSERT_EQ(9, CountFiles());

  for (int i = 0; i < 10; i++)
  {
    std::string uuid = "uuid-" + std::to_string(i);
    std::string expected = "Value " + std::to_string(i * 2);

    if (i == 5)
    {
      ASSERT_NE(OrthancPluginErrorCode_Success, ReadWhole(buffer, uuid));
    }
    else
    {
      ASSERT_EQ(OrthancPluginErrorCode_Success, ReadWhole(buffer, uuid));
      ASSERT_EQ(expected, buffer);
    }
  }

  for (int i = 0; i < 10; i++)
  {
    if (i != 5)
    {
      ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("uuid-" + std::to_string(i)));
    }
  }

  ASSERT_EQ(0, CountFiles());
}


TEST_F(MongoDBStorageTest, LargeFile)
{
  // Spans several GridFS chunks
  std::string content;
  content.resize(3 * 261120 + 17);
  for (size_t i = 0; i < content.size(); i++)
  {
    content[i] = static_cast<char>(i % 251);
  }

  ASSERT_EQ(OrthancPluginErrorCode_Success, Create("large", content));

  std::string s;
  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadWhole(s, "large"));
  ASSERT_EQ(content, s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "large", 261120 - 5, 10));
  ASSERT_EQ(content.substr(261120 - 5, 10), s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "large", 2 * 261120 + 3, 261120 + 14));
  ASSERT_EQ(content.substr(2 * 261120 + 3), s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("large"));
}


TEST_F(MongoDBStorageTest, StorageReadRange)
{
  ASSERT_EQ(OrthancPluginErrorCode_Success, Create("range", std::string("abcd\0\1\2\3\4\5", 10)));

  std::string s;
  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadWhole(s, "range"));
  ASSERT_EQ(10u, s.size());
  ASSERT_EQ('a', s[0]);
  ASSERT_EQ('d', s[3]);
  ASSERT_EQ('\0', s[4]);
  ASSERT_EQ('\5', s[9]);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "range", 0, 0));
  ASSERT_TRUE(s.empty());

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "range", 0, 1));
  ASSERT_EQ(1u, s.size());
  ASSERT_EQ('a', s[0]);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "range", 4, 1));
  ASSERT_EQ(1u, s.size());
  ASSERT_EQ('\0', s[0]);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "range", 9, 1));
  ASSERT_EQ(1u, s.size());
  ASSERT_EQ('\5', s[0]);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "range", 0, 4));
  ASSERT_EQ("abcd", s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "range", 4, 6));
  ASSERT_EQ(std::string("\0\1\2\3\4\5", 6), s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("range"));
}


// Bug 13 (fixed in Phase 8): a range past the end of the file must fail, not return garbage
TEST_F(MongoDBStorageTest, ReadRangePastEnd)
{
  ASSERT_EQ(OrthancPluginErrorCode_Success, Create("pastend", std::string("abcd\0\1\2\3\4\5", 10)));

  std::string s;
  ASSERT_NE(OrthancPluginErrorCode_Success, ReadRange(s, "pastend", 10, 1));
  ASSERT_NE(OrthancPluginErrorCode_Success, ReadRange(s, "pastend", 4, 7));

  ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("pastend"));
}


TEST_F(MongoDBStorageTest, RemoveMissing)
{
  ASSERT_NE(OrthancPluginErrorCode_Success, Remove("missing"));
}


// Bug 13 (fixed in Phase 8): a file must only be found by its exact name
TEST_F(MongoDBStorageTest, UuidPrefix)
{
  ASSERT_EQ(OrthancPluginErrorCode_Success, Create("prefix-long", "hello"));

  std::string s;
  ASSERT_NE(OrthancPluginErrorCode_Success, ReadWhole(s, "prefix"));
  ASSERT_NE(OrthancPluginErrorCode_Success, Remove("prefix"));

  ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("prefix-long"));
}


// Phase 8: the files of the previous versions (legacy GridFS API of the C driver) are read and removed unchanged
TEST_F(MongoDBStorageTest, LegacyFile)
{
  std::string content;
  content.resize(3 * 1000 + 17);  // Another chunk size than the plugin
  for (size_t i = 0; i < content.size(); i++)
  {
    content[i] = static_cast<char>((i * 7) % 256);
  }

  {
    // As "MongoDBStorageArea::Accessor::Create()" did up to Orthanc 1.11
    mongoc_client_t* client = mongoc_client_new(database_->GetUri().c_str());
    ASSERT_TRUE(client != NULL);

    mongoc_gridfs_t* gridfs = mongoc_client_get_gridfs(client, database_->GetName().c_str(), NULL, NULL);
    ASSERT_TRUE(gridfs != NULL);

    const std::string filename = MongoDBStorageArea::GetFileName("legacy", OrthancPluginContentType_Unknown);

    mongoc_gridfs_file_opt_t options;
    memset(&options, 0, sizeof(options));
    options.chunk_size = 1000;
    options.filename = filename.c_str();

    mongoc_gridfs_file_t* file = mongoc_gridfs_create_file(gridfs, &options);
    ASSERT_TRUE(file != NULL);

    mongoc_stream_t* stream = mongoc_stream_gridfs_new(file);
    ASSERT_TRUE(stream != NULL);

    mongoc_iovec_t iov;
    iov.iov_len = content.size();
    iov.iov_base = &content[0];
    ASSERT_EQ(static_cast<ssize_t>(content.size()), mongoc_stream_writev(stream, &iov, 1, 0));
    ASSERT_TRUE(mongoc_gridfs_file_save(file));

    mongoc_stream_destroy(stream);
    mongoc_gridfs_file_destroy(file);
    mongoc_gridfs_destroy(gridfs);
    mongoc_client_destroy(client);
  }

  ASSERT_EQ(1, CountFiles());
  ASSERT_EQ(4, CountChunks());

  std::string s;
  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadWhole(s, "legacy"));
  ASSERT_EQ(content, s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "legacy", 995, 10));
  ASSERT_EQ(content.substr(995, 10), s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "legacy", 2000, 1017));
  ASSERT_EQ(content.substr(2000), s);

  ASSERT_NE(OrthancPluginErrorCode_Success, ReadRange(s, "legacy", 2000, 1018));

  ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("legacy"));
  ASSERT_EQ(0, CountFiles());
  ASSERT_EQ(0, CountChunks());
}


// Phase 8: a missing or truncated chunk makes the read fail, instead of giving wrong content
TEST_F(MongoDBStorageTest, CorruptedFile)
{
  const std::string content(3 * 261120, 'x');
  ASSERT_EQ(OrthancPluginErrorCode_Success, Create("corrupted", content));

  {
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    mongocxx::database db = client[database_->GetName()];

    std::optional<bsoncxx::document::value> file = db["fs.files"].find_one(
      make_document(kvp("filename", MongoDBStorageArea::GetFileName("corrupted", OrthancPluginContentType_Unknown))));
    ASSERT_TRUE(file.has_value());

    const bsoncxx::types::bson_value::view id = file->view()["_id"].get_value();
    ASSERT_EQ(1, db["fs.chunks"].delete_one(make_document(kvp("files_id", id), kvp("n", 1)))->deleted_count());

    const std::string truncated = "xxxxx";
    ASSERT_EQ(1, db["fs.chunks"].update_one(
                make_document(kvp("files_id", id), kvp("n", 2)),
                make_document(kvp("$set", make_document(kvp("data", MongoDBToolbox::ToBinary(truncated))))))->modified_count());
  }

  std::string s;
  ASSERT_NE(OrthancPluginErrorCode_Success, ReadWhole(s, "corrupted"));
  ASSERT_NE(OrthancPluginErrorCode_Success, ReadRange(s, "corrupted", 261120 + 5, 10));    // Missing chunk
  ASSERT_NE(OrthancPluginErrorCode_Success, ReadRange(s, "corrupted", 2 * 261120, 1));     // Truncated chunk

  // The intact chunk can still be read
  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadRange(s, "corrupted", 10, 20));
  ASSERT_EQ(content.substr(10, 20), s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("corrupted"));
  ASSERT_EQ(0, CountFiles());
  ASSERT_EQ(0, CountChunks());
}


// Phase 8: an attachment stored twice (a "Create()" retried by Orthanc) is read from its last file, and removed entirely
TEST_F(MongoDBStorageTest, SameAttachmentTwice)
{
  ASSERT_EQ(OrthancPluginErrorCode_Success, Create("twice", "first"));
  ASSERT_EQ(OrthancPluginErrorCode_Success, Create("twice", "second"));
  ASSERT_EQ(2, CountFiles());

  std::string s;
  ASSERT_EQ(OrthancPluginErrorCode_Success, ReadWhole(s, "twice"));
  ASSERT_EQ("second", s);

  ASSERT_EQ(OrthancPluginErrorCode_Success, Remove("twice"));
  ASSERT_EQ(0, CountFiles());
  ASSERT_EQ(0, CountChunks());
}


// Phase 8: the storage area runs concurrently, each accessor on its own client of the pool
TEST_F(MongoDBStorageTest, ConcurrentStorage)
{
  RunConcurrently(8, [&](size_t thread)
  {
    for (int i = 0; i < 20; i++)
    {
      const std::string uuid = "concurrent-" + std::to_string(thread) + "-" + std::to_string(i);
      const std::string content(1000 + 30011 * i, static_cast<char>('a' + thread));

      std::string s;
      if (Create(uuid, content) != OrthancPluginErrorCode_Success ||
          ReadWhole(s, uuid) != OrthancPluginErrorCode_Success ||
          s != content ||
          ReadRange(s, uuid, 500, 400) != OrthancPluginErrorCode_Success ||
          s != content.substr(500, 400) ||
          Remove(uuid) != OrthancPluginErrorCode_Success)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError, "Storage failure on " + uuid);
      }
    }
  });

  ASSERT_EQ(0, CountFiles());
  ASSERT_EQ(0, CountChunks());
}


/**
 * Regression tests for the defects of "PLAN.md"
 **/

// Bug 1 (fixed in Phase 4): the attachments must be signalled before being deleted
TEST_F(MongoDBIndexTest, DeletedAttachmentsAreSignalled)
{
  std::vector<int64_t> ids = CreateHierarchy("");
  AddAttachment(ids[3], "instance-file");
  AddAttachment(ids[1], "study-file");

  {
    RecordingOutput output;
    index_->DeleteAttachment(output, *manager_, ids[1], OrthancPluginContentType_Dicom);
    ASSERT_EQ(1u, output.deletedAttachments_.size());
    ASSERT_EQ(1u, output.deletedAttachments_.count("study-file"));
  }

  {
    RecordingOutput output;
    index_->DeleteResource(output, *manager_, ids[0]);
    ASSERT_EQ(1u, output.deletedAttachments_.size());
    ASSERT_EQ(1u, output.deletedAttachments_.count("instance-file"));
    ASSERT_EQ(4u, output.deletedResources_.size());
  }

  ASSERT_EQ(0, CountDocuments("AttachedFiles"));
}


// Bug 3 (fixed in Phase 4): the ancestors left without children are deleted,
// and the remaining ancestor is the closest one that is kept
TEST_F(MongoDBIndexTest, EmptyParentsAreDeleted)
{
  std::vector<int64_t> ids = CreateHierarchy("");
  int64_t otherStudy = index_->CreateResource(*manager_, "other-study", OrthancPluginResourceType_Study);
  index_->AttachChild(*manager_, ids[0], otherStudy);

  RecordingOutput output;
  index_->DeleteResource(output, *manager_, ids[3]);

  ASSERT_EQ(3u, output.deletedResources_.size());
  ASSERT_EQ(OrthancPluginResourceType_Instance, output.deletedResources_["instance"]);
  ASSERT_EQ(OrthancPluginResourceType_Series, output.deletedResources_["series"]);
  ASSERT_EQ(OrthancPluginResourceType_Study, output.deletedResources_["study"]);
  ASSERT_EQ("patient", output.remainingAncestor_);
  ASSERT_EQ(OrthancPluginResourceType_Patient, output.remainingAncestorType_);
  ASSERT_EQ(2u, index_->GetAllResourcesCount(*manager_));
}


// Bug 4 (fixed in Phase 4): several tag constraints are combined with AND
TEST_F(MongoDBIndexTest, LookupCombinesTagsWithAnd)
{
  int64_t a = index_->CreateResource(*manager_, "a", OrthancPluginResourceType_Study);
  int64_t b = index_->CreateResource(*manager_, "b", OrthancPluginResourceType_Study);

  // StudyDescription (0008,1030) and AccessionNumber (0008,0050)
  index_->SetMainDicomTag(*manager_, a, 0x0008, 0x1030, "CT");
  index_->SetMainDicomTag(*manager_, a, 0x0008, 0x0050, "1");
  index_->SetMainDicomTag(*manager_, b, 0x0008, 0x1030, "CT");
  index_->SetMainDicomTag(*manager_, b, 0x0008, 0x0050, "2");

  DatabaseConstraints constraints;
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0008, 0x1030, false, ConstraintType_Equal, "CT", true));
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0008, 0x0050, false, ConstraintType_Equal, "2", true));

  RecordingOutput output;
  Lookup(output, OrthancPluginResourceType_Study, constraints);
  ASSERT_EQ(1u, output.matches_.size());
  ASSERT_EQ(1u, output.matches_.count("b"));
}


TEST_F(MongoDBIndexTest, LookupIdentifiersWithAnd)
{
  int64_t a = index_->CreateResource(*manager_, "a", OrthancPluginResourceType_Study);
  int64_t b = index_->CreateResource(*manager_, "b", OrthancPluginResourceType_Study);

  // PatientID (0010,0020) and StudyInstanceUID (0020,000d)
  index_->SetIdentifierTag(*manager_, a, 0x0010, 0x0020, "patient");
  index_->SetIdentifierTag(*manager_, a, 0x0020, 0x000d, "1.2.3");
  index_->SetIdentifierTag(*manager_, b, 0x0010, 0x0020, "patient");
  index_->SetIdentifierTag(*manager_, b, 0x0020, 0x000d, "1.2.4");

  DatabaseConstraints constraints;
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0010, 0x0020, true, ConstraintType_Equal, "patient", true));
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0020, 0x000d, true, ConstraintType_Equal, "1.2.4", true));

  RecordingOutput output;
  Lookup(output, OrthancPluginResourceType_Study, constraints);
  ASSERT_EQ(1u, output.matches_.size());
  ASSERT_EQ(1u, output.matches_.count("b"));
}


// Bug 4 (fixed in Phase 4): the case sensitivity of each constraint is honored
TEST_F(MongoDBIndexTest, LookupCaseSensitivity)
{
  int64_t a = index_->CreateResource(*manager_, "a", OrthancPluginResourceType_Study);
  index_->SetMainDicomTag(*manager_, a, 0x0008, 0x1030, "Brain CT");

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0008, 0x1030, false, ConstraintType_Wildcard, "brain*", true));
    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Study, constraints);
    ASSERT_EQ(0u, output.matches_.size());
  }

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0008, 0x1030, false, ConstraintType_Wildcard, "brain*", false));
    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Study, constraints);
    ASSERT_EQ(1u, output.matches_.size());
  }

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0008, 0x1030, false, ConstraintType_Equal, "brain ct", false));
    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Study, constraints);
    ASSERT_EQ(1u, output.matches_.size());
  }
}


// Bug 4 (fixed in Phase 4): the regular expression characters are escaped
TEST_F(MongoDBIndexTest, LookupEscapesRegex)
{
  int64_t a = index_->CreateResource(*manager_, "a", OrthancPluginResourceType_Study);
  int64_t b = index_->CreateResource(*manager_, "b", OrthancPluginResourceType_Study);
  index_->SetMainDicomTag(*manager_, a, 0x0008, 0x1030, "a+b (x)");
  index_->SetMainDicomTag(*manager_, b, 0x0008, 0x1030, "aab x");

  DatabaseConstraints constraints;
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0008, 0x1030, false, ConstraintType_Wildcard, "a+b (*)", true));

  RecordingOutput output;
  Lookup(output, OrthancPluginResourceType_Study, constraints);
  ASSERT_EQ(1u, output.matches_.size());
  ASSERT_EQ(1u, output.matches_.count("a"));
}


// Bug 4 (fixed in Phase 4): "limit" is the maximum number of answers
TEST_F(MongoDBIndexTest, LookupLimit)
{
  for (int i = 0; i < 5; i++)
  {
    int64_t id = index_->CreateResource(*manager_, ("s" + std::to_string(i)).c_str(), OrthancPluginResourceType_Study);
    index_->SetMainDicomTag(*manager_, id, 0x0008, 0x1030, "CT");
  }

  DatabaseConstraints constraints;
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0008, 0x1030, false, ConstraintType_Equal, "CT", true));

  {
    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Study, constraints, 3);
    ASSERT_EQ(3u, output.countMatches_);
  }

  {
    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Study, constraints, 0);  // No limit
    ASSERT_EQ(5u, output.countMatches_);
  }
}


// Tags used by the lookup tests below
static const uint16_t GROUP_0008 = 0x0008;
static const uint16_t MODALITY = 0x0060;             // (0008,0060), series
static const uint16_t STUDY_DESCRIPTION = 0x1030;    // (0008,1030), study
static const uint16_t SERIES_DESCRIPTION = 0x103e;   // (0008,103e), series
static const uint16_t GROUP_0010 = 0x0010;
static const uint16_t PATIENT_NAME = 0x0010;         // (0010,0010), patient
static const uint16_t GROUP_0020 = 0x0020;
static const uint16_t INSTANCE_NUMBER = 0x0013;      // (0020,0013), instance


// A constraint on an ancestor level applies to the only ancestor of that level
TEST_F(MongoDBIndexTest, LookupAncestorConstraint)
{
  int64_t p1 = CreateChild("p1", OrthancPluginResourceType_Patient, -1);
  int64_t p2 = CreateChild("p2", OrthancPluginResourceType_Patient, -1);
  int64_t s1 = CreateChild("s1", OrthancPluginResourceType_Study, p1);
  int64_t s2 = CreateChild("s2", OrthancPluginResourceType_Study, p1);
  int64_t s3 = CreateChild("s3", OrthancPluginResourceType_Study, p2);

  index_->SetMainDicomTag(*manager_, p1, GROUP_0010, PATIENT_NAME, "DOE^JOHN");
  index_->SetMainDicomTag(*manager_, p2, GROUP_0010, PATIENT_NAME, "SMITH^JANE");
  index_->SetMainDicomTag(*manager_, s1, GROUP_0008, STUDY_DESCRIPTION, "CT");
  index_->SetMainDicomTag(*manager_, s2, GROUP_0008, STUDY_DESCRIPTION, "MR");
  index_->SetMainDicomTag(*manager_, s3, GROUP_0008, STUDY_DESCRIPTION, "CT");

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Patient, GROUP_0010, PATIENT_NAME, false, ConstraintType_Equal, "DOE^JOHN", true));
    ASSERT_EQ(MakeSet("s1", "s2"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Patient, GROUP_0010, PATIENT_NAME, false, ConstraintType_Equal, "DOE^JOHN", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Equal, "CT", true));
    ASSERT_EQ(MakeSet("s1"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    // The driver is the study constraint (equality), the patient constraint is a filter
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Patient, GROUP_0010, PATIENT_NAME, false, ConstraintType_Wildcard, "*jane*", false));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Equal, "CT", true));
    ASSERT_EQ(MakeSet("s3"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }
}


/**
 * The way DICOMweb resolves each instance it retrieves: study, series and
 * SOP instance UIDs, in 4 copies of a study that keep the SOP instance
 * UIDs. The study (1 match) and the series (1 match) yield all their
 * instances, so the SOP instance UID (4 matches) must drive.
 **/
TEST_F(MongoDBIndexTest, LookupAncestorIdentifiersWithCopiedInstances)
{
  static const uint16_t STUDY_INSTANCE_UID = 0x000d;   // (0020,000d), study
  static const uint16_t SERIES_INSTANCE_UID = 0x000e;  // (0020,000e), series
  static const uint16_t SOP_INSTANCE_UID = 0x0018;     // (0008,0018), instance

  for (int copy = 0; copy < 4; copy++)
  {
    const std::string c = std::to_string(copy);
    int64_t patient = CreateChild("p" + c, OrthancPluginResourceType_Patient, -1);
    int64_t study = CreateChild("st" + c, OrthancPluginResourceType_Study, patient);
    int64_t series = CreateChild("se" + c, OrthancPluginResourceType_Series, study);
    index_->SetIdentifierTag(*manager_, study, GROUP_0020, STUDY_INSTANCE_UID, ("1.2.study." + c).c_str());
    index_->SetIdentifierTag(*manager_, series, GROUP_0020, SERIES_INSTANCE_UID, ("1.2.series." + c).c_str());

    for (int i = 0; i < 20; i++)
    {
      int64_t instance = CreateChild("i" + c + "-" + std::to_string(i), OrthancPluginResourceType_Instance, series);
      index_->SetIdentifierTag(*manager_, instance, GROUP_0008, SOP_INSTANCE_UID, ("1.2.sop." + std::to_string(i)).c_str());
    }
  }

  // A study without instances, whose UID matches fewer tags than the SOP instance UID
  int64_t emptyPatient = CreateChild("empty-p", OrthancPluginResourceType_Patient, -1);
  int64_t emptyStudy = CreateChild("empty-st", OrthancPluginResourceType_Study, emptyPatient);
  index_->SetIdentifierTag(*manager_, emptyStudy, GROUP_0020, STUDY_INSTANCE_UID, "1.2.study.empty");

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0020, STUDY_INSTANCE_UID, true, ConstraintType_Equal, "1.2.study.2", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0020, SERIES_INSTANCE_UID, true, ConstraintType_Equal, "1.2.series.2", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Instance, GROUP_0008, SOP_INSTANCE_UID, true, ConstraintType_Equal, "1.2.sop.7", true));
    ASSERT_EQ(MakeSet("i2-7"), LookupMatches(OrthancPluginResourceType_Instance, constraints));
  }

  {
    // The series of another copy: no match
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0020, STUDY_INSTANCE_UID, true, ConstraintType_Equal, "1.2.study.2", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0020, SERIES_INSTANCE_UID, true, ConstraintType_Equal, "1.2.series.3", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Instance, GROUP_0008, SOP_INSTANCE_UID, true, ConstraintType_Equal, "1.2.sop.7", true));
    ASSERT_TRUE(LookupMatches(OrthancPluginResourceType_Instance, constraints).empty());
  }

  {
    // The series alone, with a SOP instance UID of each copy
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0020, SERIES_INSTANCE_UID, true, ConstraintType_Equal, "1.2.series.1", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Instance, GROUP_0008, SOP_INSTANCE_UID, true, ConstraintType_Equal, "1.2.sop.0", true));
    ASSERT_EQ(MakeSet("i1-0"), LookupMatches(OrthancPluginResourceType_Instance, constraints));
  }

  {
    // The study has no instances: its candidates are counted as 0
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0020, STUDY_INSTANCE_UID, true, ConstraintType_Equal, "1.2.study.empty", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Instance, GROUP_0008, SOP_INSTANCE_UID, true, ConstraintType_Equal, "1.2.sop.0", true));
    ASSERT_TRUE(LookupMatches(OrthancPluginResourceType_Instance, constraints).empty());
  }

  {
    // The study drives when it has fewer instances than the SOP instance UID has matches
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0020, STUDY_INSTANCE_UID, true, ConstraintType_Equal, "1.2.study.0", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Instance, GROUP_0008, SOP_INSTANCE_UID, true, ConstraintType_Wildcard, "1.2.sop.*", true));
    ASSERT_EQ(20u, LookupMatches(OrthancPluginResourceType_Instance, constraints).size());
  }
}


// A constraint on a lower level matches the resources that have a matching descendant, answered once
TEST_F(MongoDBIndexTest, LookupDescendantConstraint)
{
  int64_t p = CreateChild("p", OrthancPluginResourceType_Patient, -1);
  int64_t s1 = CreateChild("s1", OrthancPluginResourceType_Study, p);
  int64_t s2 = CreateChild("s2", OrthancPluginResourceType_Study, p);
  int64_t a = CreateChild("a", OrthancPluginResourceType_Series, s1);
  int64_t b = CreateChild("b", OrthancPluginResourceType_Series, s1);
  int64_t c = CreateChild("c", OrthancPluginResourceType_Series, s1);
  int64_t d = CreateChild("d", OrthancPluginResourceType_Series, s2);

  index_->SetMainDicomTag(*manager_, a, GROUP_0008, MODALITY, "CT");
  index_->SetMainDicomTag(*manager_, b, GROUP_0008, MODALITY, "CT");
  index_->SetMainDicomTag(*manager_, c, GROUP_0008, MODALITY, "MR");
  index_->SetMainDicomTag(*manager_, d, GROUP_0008, MODALITY, "MR");

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, false, ConstraintType_Equal, "CT", true));
    ASSERT_EQ(MakeSet("s1"), LookupMatches(OrthancPluginResourceType_Study, constraints));
    ASSERT_EQ(MakeSet("p"), LookupMatches(OrthancPluginResourceType_Patient, constraints));
  }

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, false, ConstraintType_Equal, "MR", true));
    ASSERT_EQ(MakeSet("s1", "s2"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    // "ModalitiesInStudy", as sent by Orthanc
    DatabaseConstraints constraints;
    std::vector<std::string> modalities;
    modalities.push_back("CT");
    modalities.push_back("US");
    constraints.AddConstraint(CreateListConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, modalities, true));
    ASSERT_EQ(MakeSet("s1"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }
}


// Like the "INNER JOIN" of the SQL plugins, one descendant must match all the constraints of its level
TEST_F(MongoDBIndexTest, LookupDescendantsMatchTogether)
{
  int64_t s1 = CreateChild("s1", OrthancPluginResourceType_Study, -1);
  int64_t s2 = CreateChild("s2", OrthancPluginResourceType_Study, -1);
  int64_t a = CreateChild("a", OrthancPluginResourceType_Series, s1);
  int64_t b = CreateChild("b", OrthancPluginResourceType_Series, s1);
  int64_t c = CreateChild("c", OrthancPluginResourceType_Series, s2);

  index_->SetMainDicomTag(*manager_, a, GROUP_0008, MODALITY, "CT");
  index_->SetMainDicomTag(*manager_, a, GROUP_0008, SERIES_DESCRIPTION, "head");
  index_->SetMainDicomTag(*manager_, b, GROUP_0008, MODALITY, "MR");
  index_->SetMainDicomTag(*manager_, b, GROUP_0008, SERIES_DESCRIPTION, "chest");
  index_->SetMainDicomTag(*manager_, c, GROUP_0008, MODALITY, "CT");
  index_->SetMainDicomTag(*manager_, c, GROUP_0008, SERIES_DESCRIPTION, "chest");

  // "s1" has a CT series and a chest series, but not a CT chest series
  DatabaseConstraints constraints;
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, false, ConstraintType_Equal, "CT", true));
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0008, SERIES_DESCRIPTION, false, ConstraintType_Equal, "chest", true));
  ASSERT_EQ(MakeSet("s2"), LookupMatches(OrthancPluginResourceType_Study, constraints));
}


// The descendants of several lower levels form one chain
TEST_F(MongoDBIndexTest, LookupDescendantChain)
{
  int64_t s = CreateChild("s", OrthancPluginResourceType_Study, -1);
  int64_t a = CreateChild("a", OrthancPluginResourceType_Series, s);
  int64_t b = CreateChild("b", OrthancPluginResourceType_Series, s);
  int64_t i1 = CreateChild("i1", OrthancPluginResourceType_Instance, a);
  int64_t i2 = CreateChild("i2", OrthancPluginResourceType_Instance, b);

  index_->SetMainDicomTag(*manager_, a, GROUP_0008, MODALITY, "CT");
  index_->SetMainDicomTag(*manager_, b, GROUP_0008, MODALITY, "MR");
  index_->SetMainDicomTag(*manager_, i1, GROUP_0020, INSTANCE_NUMBER, "1");
  index_->SetMainDicomTag(*manager_, i2, GROUP_0020, INSTANCE_NUMBER, "2");

  {
    // The instance "1" belongs to the CT series, not to the MR series
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, false, ConstraintType_Equal, "MR", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Instance, GROUP_0020, INSTANCE_NUMBER, false, ConstraintType_Equal, "1", true));
    ASSERT_TRUE(LookupMatches(OrthancPluginResourceType_Study, constraints).empty());
  }

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, false, ConstraintType_Equal, "MR", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Instance, GROUP_0020, INSTANCE_NUMBER, false, ConstraintType_Equal, "2", true));
    ASSERT_EQ(MakeSet("s"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    // Query on the series level, with an ancestor, a series and an instance constraint
    index_->SetMainDicomTag(*manager_, s, GROUP_0008, STUDY_DESCRIPTION, "study");

    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Equal, "study", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, false, ConstraintType_Wildcard, "?T", true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Instance, GROUP_0020, INSTANCE_NUMBER, false, ConstraintType_GreaterOrEqual, "1", true));
    ASSERT_EQ(MakeSet("a"), LookupMatches(OrthancPluginResourceType_Series, constraints));
  }
}


// Several constraints on the same tag apply to the same value, e.g. the two bounds of a date range
TEST_F(MongoDBIndexTest, LookupRange)
{
  const char* dates[] = { "20200101", "20200615", "20201231", "20210101" };
  for (int i = 0; i < 4; i++)
  {
    int64_t id = CreateChild(dates[i], OrthancPluginResourceType_Study, -1);
    index_->SetMainDicomTag(*manager_, id, GROUP_0008, 0x0020 /* StudyDate */, dates[i]);
  }

  DatabaseConstraints constraints;
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, 0x0020, false, ConstraintType_GreaterOrEqual, "20200601", true));
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, 0x0020, false, ConstraintType_SmallerOrEqual, "20201231", true));
  ASSERT_EQ(MakeSet("20200615", "20201231"), LookupMatches(OrthancPluginResourceType_Study, constraints));
}


// As "lower(value) <= lower(x)" in SQL; a value starting with "$" is not a field path
TEST_F(MongoDBIndexTest, LookupCaseInsensitiveRange)
{
  int64_t a = CreateChild("a", OrthancPluginResourceType_Study, -1);
  int64_t b = CreateChild("b", OrthancPluginResourceType_Study, -1);
  index_->SetMainDicomTag(*manager_, a, GROUP_0008, STUDY_DESCRIPTION, "abc");
  index_->SetMainDicomTag(*manager_, b, GROUP_0008, STUDY_DESCRIPTION, "XYZ");

  {
    // Case-sensitive: the capitals sort before the lowercase letters
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_SmallerOrEqual, "b", true));
    ASSERT_EQ(MakeSet("a", "b"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_SmallerOrEqual, "B", false));
    ASSERT_EQ(MakeSet("a"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    DatabaseConstraints constraints;
    // Read as the field path "$value", the comparison would always be true
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_SmallerOrEqual, "$value", false));
    ASSERT_TRUE(LookupMatches(OrthancPluginResourceType_Study, constraints).empty());
  }
}


TEST_F(MongoDBIndexTest, LookupList)
{
  int64_t a = CreateChild("a", OrthancPluginResourceType_Series, -1);
  int64_t b = CreateChild("b", OrthancPluginResourceType_Series, -1);
  int64_t c = CreateChild("c", OrthancPluginResourceType_Series, -1);
  index_->SetMainDicomTag(*manager_, a, GROUP_0008, MODALITY, "CT");
  index_->SetMainDicomTag(*manager_, b, GROUP_0008, MODALITY, "mr");
  index_->SetMainDicomTag(*manager_, c, GROUP_0008, MODALITY, "US");

  std::vector<std::string> values;
  values.push_back("CT");
  values.push_back("MR");

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateListConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, values, true));
    ASSERT_EQ(MakeSet("a"), LookupMatches(OrthancPluginResourceType_Series, constraints));
  }

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateListConstraint(Orthanc::ResourceType_Series, GROUP_0008, MODALITY, values, false));
    ASSERT_EQ(MakeSet("a", "b"), LookupMatches(OrthancPluginResourceType_Series, constraints));
  }
}


// A constraint that is not mandatory also matches the resources without the tag ("value IS NULL OR ..." in SQL)
TEST_F(MongoDBIndexTest, LookupNotMandatory)
{
  int64_t a = CreateChild("a", OrthancPluginResourceType_Study, -1);
  int64_t b = CreateChild("b", OrthancPluginResourceType_Study, -1);
  CreateChild("c", OrthancPluginResourceType_Study, -1);  // Without the tag
  index_->SetMainDicomTag(*manager_, a, GROUP_0008, STUDY_DESCRIPTION, "CT");
  index_->SetMainDicomTag(*manager_, b, GROUP_0008, STUDY_DESCRIPTION, "MR");

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Equal, "CT", true, false));
    ASSERT_EQ(MakeSet("a", "c"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    // The universal constraint on an optional tag is ignored
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Wildcard, "*", true, false));
    ASSERT_EQ(MakeSet("a", "b", "c"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    // The universal constraint on a mandatory tag needs the tag
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Wildcard, "*", true, true));
    ASSERT_EQ(MakeSet("a", "b"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }

  {
    // Mandatory and optional constraints together
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Wildcard, "*", true, true));
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Equal, "MR", true, false));
    ASSERT_EQ(MakeSet("b"), LookupMatches(OrthancPluginResourceType_Study, constraints));
  }
}


TEST_F(MongoDBIndexTest, LookupWithoutConstraint)
{
  CreateHierarchy("a");
  CreateHierarchy("b");

  DatabaseConstraints constraints;
  ASSERT_EQ(MakeSet("apatient", "bpatient"), LookupMatches(OrthancPluginResourceType_Patient, constraints));
  ASSERT_EQ(MakeSet("aseries", "bseries"), LookupMatches(OrthancPluginResourceType_Series, constraints));

  RecordingOutput output;
  Lookup(output, OrthancPluginResourceType_Instance, constraints, 1);
  ASSERT_EQ(1u, output.countMatches_);
}


// A tag document of a resource of another level does not match
TEST_F(MongoDBIndexTest, LookupIgnoresTagsOfOtherLevels)
{
  std::vector<int64_t> ids = CreateHierarchy("");
  index_->SetMainDicomTag(*manager_, ids[2] /* series */, GROUP_0008, STUDY_DESCRIPTION, "CT");

  DatabaseConstraints constraints;
  constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Equal, "CT", true));
  ASSERT_TRUE(LookupMatches(OrthancPluginResourceType_Study, constraints).empty());
}


// Studies and series are answered from the most recent, as in the previous versions of the plugin
TEST_F(MongoDBIndexTest, LookupSortsStudies)
{
  // "sorts" holds the StudyDate, then the StudyTime
  const char* names[] = { "old", "morning", "middle", "evening" };
  const char* dates[] = { "20200101", "20220101", "20210101", "20220101" };
  const char* times[] = { "230000", "080000", "230000", "170000" };
  for (int i = 0; i < 4; i++)
  {
    int64_t id = CreateChild(names[i], OrthancPluginResourceType_Study, -1);
    OrthancPluginResourcesContentTags tags[2];
    tags[0].resource = id;
    tags[0].group = GROUP_0008;
    tags[0].element = 0x0020;  // StudyDate
    tags[0].value = dates[i];
    tags[1].resource = id;
    tags[1].group = GROUP_0008;
    tags[1].element = 0x0030;  // StudyTime
    tags[1].value = times[i];
    index_->SetResourcesContent(*manager_, 0, NULL, 2, tags, 0, NULL);
  }

  DatabaseConstraints constraints;

  {
    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Study, constraints, 1);
    ASSERT_EQ(MakeSet("evening"), output.matches_);
  }

  {
    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Study, constraints, 3);
    ASSERT_EQ(MakeSet("evening", "morning", "middle"), output.matches_);
  }
}


TEST_F(MongoDBIndexTest, LookupSomeInstance)
{
  std::vector<int64_t> a = CreateHierarchy("a");
  std::vector<int64_t> b = CreateHierarchy("b");
  index_->SetMainDicomTag(*manager_, a[1], GROUP_0008, STUDY_DESCRIPTION, "CT");
  index_->SetMainDicomTag(*manager_, b[1], GROUP_0008, STUDY_DESCRIPTION, "CT");

  {
    DatabaseConstraints constraints;
    constraints.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, GROUP_0008, STUDY_DESCRIPTION, false, ConstraintType_Equal, "CT", true));

    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Study, constraints, 0, true);
    ASSERT_EQ(2u, output.someInstances_.size());
    ASSERT_EQ("ainstance", output.someInstances_["astudy"]);
    ASSERT_EQ("binstance", output.someInstances_["bstudy"]);
  }

  {
    DatabaseConstraints constraints;
    RecordingOutput output;
    Lookup(output, OrthancPluginResourceType_Patient, constraints, 0, true);
    ASSERT_EQ("ainstance", output.someInstances_["apatient"]);
    ASSERT_EQ("binstance", output.someInstances_["bpatient"]);

    RecordingOutput output2;
    Lookup(output2, OrthancPluginResourceType_Instance, constraints, 0, true);
    ASSERT_EQ("ainstance", output2.someInstances_["ainstance"]);
  }
}


// Bug 5 (fixed in Phase 4): the patient to recycle is the oldest one
TEST_F(MongoDBIndexTest, SelectPatientToRecycleIsSorted)
{
  int64_t p1 = index_->CreateResource(*manager_, "p1", OrthancPluginResourceType_Patient);
  int64_t p2 = index_->CreateResource(*manager_, "p2", OrthancPluginResourceType_Patient);

  {
    // Store the recycling order in the reverse order of insertion
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    auto collection = GetCollection(client, "PatientRecyclingOrder");
    collection.delete_many({});
    collection.insert_one(make_document(kvp("id", int64_t(20)), kvp("patientId", p1)));
    collection.insert_one(make_document(kvp("id", int64_t(10)), kvp("patientId", p2)));
  }

  int64_t r;
  ASSERT_TRUE(index_->SelectPatientToRecycle(r, *manager_));
  ASSERT_EQ(p2, r);
}


// Bug 6 (fixed in Phase 4): the changes are sorted by their sequence number
TEST_F(MongoDBIndexTest, GetChangesIsSorted)
{
  int64_t p = index_->CreateResource(*manager_, "p", OrthancPluginResourceType_Patient);

  {
    // Store the changes in the reverse order of their sequence number
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    auto collection = GetCollection(client, "Changes");
    for (int64_t seq = 5; seq >= 1; seq--)
    {
      collection.insert_one(make_document(kvp("id", seq), kvp("changeType", 1), kvp("internalId", p),
                                          kvp("resourceType", 0), kvp("date", "20260923T000000")));
    }
  }

  RecordingOutput output;
  bool done;
  index_->GetChanges(output, done, *manager_, 1, 2);
  ASSERT_FALSE(done);
  ASSERT_EQ(2u, output.changes_.size());
  ASSERT_EQ(2, output.changes_[0]);
  ASSERT_EQ(3, output.changes_[1]);
}


// Bug 11 (Phase 5): global properties of 16MB and more (e.g. the
// serialized jobs of Orthanc) exceed the maximum size of a BSON
// document, so they are stored in GridFS
TEST_F(MongoDBIndexTest, LargeGlobalProperty)
{
  std::string longProperty;
  longProperty.resize(16 * 1024 * 1024);
  for (size_t i = 0; i < longProperty.size(); i++)
  {
    longProperty[i] = 'A' + (i % 26);
  }

  index_->SetGlobalProperty(*manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8, longProperty.c_str());
  index_->SetGlobalProperty(*manager_, "some-server", Orthanc::GlobalProperty_DatabaseInternal8, longProperty.c_str());

  std::string s;
  ASSERT_TRUE(index_->LookupGlobalProperty(s, *manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8));
  ASSERT_EQ(longProperty, s);

  s.clear();
  ASSERT_TRUE(index_->LookupGlobalProperty(s, *manager_, "some-server", Orthanc::GlobalProperty_DatabaseInternal8));
  ASSERT_EQ(longProperty, s);

  // The documents keep a string "value", which the previous versions read
  const bsoncxx::document::value filter = make_document(
    kvp("property", static_cast<int32_t>(Orthanc::GlobalProperty_DatabaseInternal8)),
    kvp("value", ""),
    kvp("valueFile", make_document(kvp("$exists", true))));
  ASSERT_EQ(1, CountDocuments("GlobalProperties", filter.view()));
  ASSERT_EQ(1, CountDocuments("ServerProperties", filter.view()));
  ASSERT_EQ(2, CountDocuments("LargeProperties.files"));

  // A new large value replaces the file of the previous one
  longProperty[0] = 'Z';
  index_->SetGlobalProperty(*manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8, longProperty.c_str());
  ASSERT_TRUE(index_->LookupGlobalProperty(s, *manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8));
  ASSERT_EQ(longProperty, s);
  ASSERT_EQ(2, CountDocuments("LargeProperties.files"));

  // A small value is stored inline again, and the files are deleted
  index_->SetGlobalProperty(*manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8, "small");
  index_->SetGlobalProperty(*manager_, "some-server", Orthanc::GlobalProperty_DatabaseInternal8, "small");
  ASSERT_TRUE(index_->LookupGlobalProperty(s, *manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8));
  ASSERT_EQ("small", s);
  ASSERT_EQ(0, CountDocuments("GlobalProperties", make_document(kvp("valueFile", make_document(kvp("$exists", true))))));
  ASSERT_EQ(0, CountDocuments("LargeProperties.files"));
  ASSERT_EQ(0, CountDocuments("LargeProperties.chunks"));
}


// Bug 7 (Phase 5): the sequence counters are atomic
TEST_F(MongoDBIndexTest, ConcurrentSequences)
{
  const size_t THREADS = 8;
  const size_t RESOURCES = 25;

  std::vector< std::vector<int64_t> > ids(THREADS);

  RunConcurrently(THREADS, [&](size_t thread)
  {
    DatabaseManager manager(index_->CreateDatabaseFactory());
    for (size_t i = 0; i < RESOURCES; i++)
    {
      std::string publicId = "t" + std::to_string(thread) + "-" + std::to_string(i);
      ids[thread].push_back(index_->CreateResource(manager, publicId.c_str(), OrthancPluginResourceType_Study));
    }
  });

  std::set<int64_t> unique;
  for (size_t i = 0; i < THREADS; i++)
  {
    unique.insert(ids[i].begin(), ids[i].end());
  }

  ASSERT_EQ(THREADS * RESOURCES, unique.size());
  ASSERT_EQ(1, CountDocuments("Sequences", make_document(kvp("name", "Resources"))));
}


// Bug 7 (Phase 5): setting a global property is an upsert
TEST_F(MongoDBIndexTest, ConcurrentGlobalProperty)
{
  RunConcurrently(8, [&](size_t thread)
  {
    DatabaseManager manager(index_->CreateDatabaseFactory());
    index_->SetGlobalProperty(manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9,
                              ("value " + std::to_string(thread)).c_str());
  });

  ASSERT_EQ(1, CountDocuments("GlobalProperties", make_document(
                                kvp("property", static_cast<int32_t>(Orthanc::GlobalProperty_DatabaseInternal9)))));
}


// Bug 7 (Phase 5): on a standalone server, concurrent uploads of the
// same instances must not create duplicate resources. Each patient is
// new, so that the threads race on the creation of its hierarchy.
TEST_F(MongoDBIndexTest, ConcurrentCreateInstance)
{
  const size_t THREADS = 8;
  const size_t PATIENTS = 25;
  const size_t INSTANCES = 4;  // Per patient

  std::atomic<size_t> newInstances(0);
  std::atomic<size_t> newPatients(0);

  RunConcurrently(THREADS, [&](size_t thread)
  {
    DatabaseManager manager(index_->CreateDatabaseFactory());
    for (size_t p = 0; p < PATIENTS; p++)
    {
      const std::string prefix = std::to_string(p) + "-";

      for (size_t i = 0; i < INSTANCES; i++)
      {
        OrthancPluginCreateInstanceResult r;
        const std::string instance = prefix + "instance-" + std::to_string(i);

        // Only the attempt that succeeds counts
        ApplyWithRetries(manager, [&]()
        {
          memset(&r, 0, sizeof(r));
          index_->CreateInstance(r, manager, (prefix + "patient").c_str(), (prefix + "study").c_str(),
                                 (prefix + "series").c_str(), instance.c_str());
        });

        if (r.isNewInstance)
        {
          newInstances++;
        }

        if (r.isNewPatient)
        {
          newPatients++;
        }
      }
    }
  });

  ASSERT_EQ(PATIENTS * INSTANCES, newInstances.load());
  ASSERT_EQ(PATIENTS, newPatients.load());
  ASSERT_EQ(PATIENTS * (3 + INSTANCES), index_->GetAllResourcesCount(*manager_));
  ASSERT_EQ(1, CountDocuments("Resources", make_document(kvp("publicId", "0-patient"))));

  // Every new patient enters the recycling order, even if the first
  // attempt to create its hierarchy failed halfway
  ASSERT_EQ(PATIENTS, index_->GetUnprotectedPatientsCount(*manager_));
  ASSERT_EQ(static_cast<int64_t>(PATIENTS), CountDocuments("PatientRecyclingOrder"));
}


// Phase 9: on a standalone server, an attachment added to a resource
// that a concurrent "DeleteResource()" has removed is not kept. The
// store is retried, as the file was not signalled for deletion.
TEST_F(MongoDBIndexTest, AttachmentOfDeletedResource)
{
  if (IsTestServerReplicaSet())
  {
    printf("Skipped: a transaction makes the store and the deletion serializable\n");
    return;
  }

  std::vector<int64_t> ids = CreateHierarchy("");

  {
    RecordingOutput output;
    index_->DeleteResource(output, *manager_, ids[0]);
  }

  try
  {
    AddAttachment(ids[3], "late");
    FAIL();
  }
  catch (Orthanc::OrthancException& e)
  {
    ASSERT_EQ(Orthanc::ErrorCode_DatabaseCannotSerialize, e.GetErrorCode());
  }

  ASSERT_EQ(0, CountDocuments("AttachedFiles"));
  ASSERT_EQ(0u, index_->GetTotalCompressedSize(*manager_));
  ASSERT_EQ(0u, index_->GetTotalUncompressedSize(*manager_));
}


// A store that fails on a resource left from a deleted parent still
// counts the ancestors that it has created before
TEST_F(MongoDBIndexTest, CountsOfAFailedStore)
{
  OrthancPluginCreateInstanceResult r;
  index_->CreateInstance(r, *manager_, "patient", "study", "series", "instance");

  {
    // The deletion of the patient, as it races a store without transactions
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    GetCollection(client, "Resources").delete_one(make_document(kvp("publicId", "patient")));
  }

  const uint64_t patients = index_->GetResourcesCount(*manager_, OrthancPluginResourceType_Patient);

  try
  {
    index_->CreateInstance(r, *manager_, "patient", "study", "series2", "instance2");
    FAIL();
  }
  catch (Orthanc::OrthancException& e)
  {
    ASSERT_EQ(Orthanc::ErrorCode_DatabaseCannotSerialize, e.GetErrorCode());
  }

  ASSERT_EQ(1, CountDocuments("Resources", make_document(kvp("publicId", "patient"))));
  ASSERT_EQ(patients + 1, index_->GetResourcesCount(*manager_, OrthancPluginResourceType_Patient));
}


/**
 * Phase 9: stores race the deletion of their patient, as a C-MOVE to
 * Orthanc itself races "DELETE /patients" in orthanc-tests. Each file
 * must end up either referenced by an attachment, or signalled for
 * deletion, or dropped by Orthanc after a failed store; never both
 * referenced and signalled (a lost file), nor neither (a leaked one).
 * The index must also stay consistent: no resource under a deleted
 * one, no attachment of a deleted resource, and statistics that match
 * the data.
 **/
TEST_F(MongoDBIndexTest, ConcurrentStoreAndDelete)
{
  const size_t STORERS = 4;
  const size_t INSTANCES = 40;  // Per storer

  std::atomic<size_t> finishedStorers(0);
  std::vector< std::set<std::string> > stored(STORERS);     // UUIDs of the successful stores
  std::vector< std::set<std::string> > failed(STORERS);     // UUIDs of the stores that failed
  std::set<std::string> signalled;
  size_t deletions = 0;

  RunConcurrently(STORERS + 1, [&](size_t thread)
  {
    DatabaseManager manager(index_->CreateDatabaseFactory());

    if (thread == STORERS)
    {
      // The deleter: deletes the patient each time it exists, until the storers are done
      while (finishedStorers < STORERS)
      {
        int64_t id;
        OrthancPluginResourceType type;
        if (index_->LookupResource(id, type, manager, "patient"))
        {
          std::unique_ptr<RecordingOutput> output;
          try
          {
            ApplyWithRetries(manager, [&]()
            {
              output.reset(new RecordingOutput);  // Orthanc drops the signals of a failed attempt
              index_->DeleteResource(*output, manager, id);
            });

            signalled.insert(output->deletedAttachments_.begin(), output->deletedAttachments_.end());
            deletions++;
          }
          catch (Orthanc::OrthancException& e)
          {
            // Another store has re-created the patient meanwhile, with a new ID
            if (e.GetErrorCode() != Orthanc::ErrorCode_UnknownResource)
            {
              throw;
            }
          }
        }

        // Leaves the storers some time to finish, as a deletion that follows each
        // new patient right away could make them run out of retries
        std::this_thread::sleep_for(std::chrono::milliseconds(5 + rand() % 20));
      }
    }
    else
    {
      for (size_t i = 0; i < INSTANCES; i++)
      {
        const std::string series = "series-" + std::to_string(i % 2);
        const std::string instance = "instance-" + std::to_string(thread) + "-" + std::to_string(i);
        const std::string uuid = "file-" + instance;  // The same file for all the attempts, as in Orthanc

        OrthancPluginAttachment attachment;
        attachment.uuid = uuid.c_str();
        attachment.contentType = OrthancPluginContentType_Dicom;
        attachment.uncompressedSize = 42;
        attachment.uncompressedHash = "md5";
        attachment.compressionType = OrthancPluginCompressionType_None;
        attachment.compressedSize = 42;
        attachment.compressedHash = "md5";

        try
        {
          ApplyWithRetries(manager, [&]()
          {
            OrthancPluginCreateInstanceResult r;
            memset(&r, 0, sizeof(r));
            index_->CreateInstance(r, manager, "patient", "study", series.c_str(), instance.c_str());
            if (!r.isNewInstance)
            {
              throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError, "Instance created twice");
            }

            index_->AddAttachment(manager, r.instanceId, attachment, 0);
          });

          stored[thread].insert(uuid);
        }
        catch (Orthanc::OrthancException& e)
        {
          // "DeleteResource()" has taken the attachment, and signalled its file
          if (e.GetErrorCode() != Orthanc::ErrorCode_UnknownResource)
          {
            finishedStorers++;
            throw;
          }

          failed[thread].insert(uuid);
        }
      }

      finishedStorers++;
    }
  });

  printf("%d deletions, %d files signalled\n", static_cast<int>(deletions), static_cast<int>(signalled.size()));

  std::set<std::string> referenced;
  std::set<int64_t> resources;
  std::map<int64_t, int64_t> parents;

  {
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};

    for (const bsoncxx::document::view& d : GetCollection(client, "Resources").find(make_document()))
    {
      const int64_t id = MongoDBToolbox::GetInteger(d, "internalId");
      resources.insert(id);

      int64_t parent;
      if (MongoDBToolbox::LookupInteger(parent, d, "parentId"))
      {
        parents[id] = parent;
      }
    }

    for (const bsoncxx::document::view& d : GetCollection(client, "AttachedFiles").find(make_document()))
    {
      ASSERT_EQ(1u, resources.count(MongoDBToolbox::GetInteger(d, "id")));
      referenced.insert(MongoDBToolbox::GetString(d, "uuid"));
    }
  }

  for (std::map<int64_t, int64_t>::const_iterator it = parents.begin(); it != parents.end(); ++it)
  {
    ASSERT_EQ(1u, resources.count(it->second));
  }

  for (size_t i = 0; i < STORERS; i++)
  {
    for (const std::string& uuid : stored[i])
    {
      ASSERT_TRUE(referenced.count(uuid) + signalled.count(uuid) == 1) << uuid;
    }

    for (const std::string& uuid : failed[i])
    {
      ASSERT_EQ(0u, referenced.count(uuid)) << uuid;
    }
  }

  ASSERT_EQ(static_cast<int64_t>(referenced.size()), CountDocuments("AttachedFiles"));
  ASSERT_EQ(referenced.size() * 42, index_->GetTotalCompressedSize(*manager_));
  ASSERT_EQ(static_cast<uint64_t>(CountDocuments("Resources", make_document(kvp("resourceType", 3)))),
            index_->GetResourcesCount(*manager_, OrthancPluginResourceType_Instance));
  ASSERT_EQ(static_cast<uint64_t>(CountDocuments("Resources", make_document(kvp("resourceType", 0)))),
            index_->GetResourcesCount(*manager_, OrthancPluginResourceType_Patient));
}


// Phase 5: a new database gets the current revision, and a restart
// finds every index already built
TEST_F(MongoDBIndexTest, SchemaRevision)
{
  ASSERT_EQ("6", GetGlobalProperty(Orthanc::GlobalProperty_DatabaseSchemaVersion));
  ASSERT_EQ(std::to_string(MongoDBSchema::SCHEMA_REVISION), GetGlobalProperty(Orthanc::GlobalProperty_DatabasePatchLevel));

  std::list<std::string> missing;
  ASSERT_TRUE(MongoDBSchema::CheckIndexes(missing, *manager_));
  ASSERT_TRUE(missing.empty());

  // The upgrade lock is released
  ASSERT_EQ(0, CountDocuments("Locks"));
}


// Phase 5: a database of the previous versions, whose data has
// duplicates: the unique index is not built, and the database stays
// at revision 1 (the plugin still runs)
TEST_F(MongoDBIndexTest, SchemaRevisionWithDuplicates)
{
  {
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    GetCollection(client, "GlobalProperties").update_one(
      make_document(kvp("property", static_cast<int32_t>(Orthanc::GlobalProperty_DatabasePatchLevel))),
      make_document(kvp("$set", make_document(kvp("value", "1")))));

    mongocxx::collection metadata = GetCollection(client, "Metadata");
    metadata.indexes().drop_one("id_1_type_1_unique");
    metadata.insert_one(make_document(kvp("id", int64_t(1)), kvp("type", 1), kvp("value", "a")));
    metadata.insert_one(make_document(kvp("id", int64_t(1)), kvp("type", 1), kvp("value", "b")));
  }

  std::unique_ptr<MongoDBIndex> index;
  std::unique_ptr<DatabaseManager> manager(StartAnotherIndex(index));

  ASSERT_EQ("1", GetGlobalProperty(Orthanc::GlobalProperty_DatabasePatchLevel));

  std::list<std::string> missing;
  ASSERT_FALSE(MongoDBSchema::CheckIndexes(missing, *manager));
  ASSERT_EQ(1u, missing.size());
  ASSERT_EQ("Metadata.id_1_type_1_unique", missing.front());

  // Once the duplicate is repaired, the next start completes the upgrade
  {
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    GetCollection(client, "Metadata").delete_one(make_document(kvp("value", "b")));
  }

  manager.reset(StartAnotherIndex(index));
  ASSERT_EQ(std::to_string(MongoDBSchema::SCHEMA_REVISION), GetGlobalProperty(Orthanc::GlobalProperty_DatabasePatchLevel));
  ASSERT_TRUE(MongoDBSchema::CheckIndexes(missing, *manager));
}


// Phase 5: after an upgrade, the previous version of the plugin still
// starts (its startup is replayed here, as in "git show 03b2f0d"), and
// the next start of this version upgrades the database again
TEST_F(MongoDBIndexTest, Downgrade)
{
  std::vector<int64_t> ids = CreateHierarchy("");

  {
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};

    // "MongoDatabase::CreateIndices()": default names and options
    GetCollection(client, "fs.files").create_index(make_document(kvp("filename", 1)));
    GetCollection(client, "Resources").create_index(make_document(kvp("parentId", 1)));
    GetCollection(client, "Resources").create_index(make_document(kvp("publicId", 1)));
    GetCollection(client, "Resources").create_index(make_document(kvp("resourceType", 1)));
    GetCollection(client, "Resources").create_index(make_document(kvp("internalId", 1)));
    GetCollection(client, "PatientRecyclingOrder").create_index(make_document(kvp("patientId", 1)));
    GetCollection(client, "MainDicomTags").create_index(make_document(kvp("id", 1)));
    GetCollection(client, "MainDicomTags").create_index(make_document(kvp("tagGroup", 1), kvp("tagElement", 1), kvp("value", 1)));
    GetCollection(client, "DicomIdentifiers").create_index(make_document(kvp("id", 1)));
    GetCollection(client, "DicomIdentifiers").create_index(make_document(kvp("tagGroup", 1), kvp("tagElement", 1), kvp("value", 1)));
    GetCollection(client, "Changes").create_index(make_document(kvp("internalId", 1)));
    GetCollection(client, "AttachedFiles").create_index(make_document(kvp("id", 1)));
    GetCollection(client, "Metadata").create_index(make_document(kvp("id", 1)));
    GetCollection(client, "GlobalProperties").create_index(make_document(kvp("property", 1)));
    GetCollection(client, "ServerProperties").create_index(make_document(kvp("server", 1), kvp("property", 1)));

    // "MongoDBIndex::SetGlobalProperty()": a replacement, or an insertion
    mongocxx::collection properties = GetCollection(client, "GlobalProperties");
    const int32_t versions[] = { Orthanc::GlobalProperty_DatabaseSchemaVersion, Orthanc::GlobalProperty_DatabasePatchLevel };
    const char* values[] = { "6", "1" };

    for (size_t i = 0; i < 2; i++)
    {
      bsoncxx::document::value replacement = make_document(kvp("property", versions[i]), kvp("value", values[i]));
      if (!properties.find_one_and_update(make_document(kvp("property", versions[i])), replacement.view()))
      {
        properties.insert_one(replacement.view());
      }
    }
  }

  ASSERT_EQ("1", GetGlobalProperty(Orthanc::GlobalProperty_DatabasePatchLevel));
  ASSERT_EQ(1, CountDocuments("GlobalProperties", make_document(
                                kvp("property", static_cast<int32_t>(Orthanc::GlobalProperty_DatabasePatchLevel)))));

  // The unique indexes are still there, so this version upgrades again at once
  std::unique_ptr<MongoDBIndex> index;
  std::unique_ptr<DatabaseManager> manager(StartAnotherIndex(index));
  ASSERT_EQ(std::to_string(MongoDBSchema::SCHEMA_REVISION), GetGlobalProperty(Orthanc::GlobalProperty_DatabasePatchLevel));

  int64_t id;
  OrthancPluginResourceType type;
  ASSERT_TRUE(index->LookupResource(id, type, *manager, "instance"));
  ASSERT_EQ(ids[3], id);
  ASSERT_EQ(4u, index->GetAllResourcesCount(*manager));
}


namespace
{
  void InsertLock(const TestDatabase& database,
                  const std::string& owner,
                  std::chrono::milliseconds expiresIn)
  {
    mongocxx::client client{mongocxx::uri{database.GetUri()}};
    client[database.GetName()]["Locks"].insert_one(make_document(
      kvp("_id", "SchemaUpgrade"),
      kvp("owner", owner),
      kvp("expiresAt", bsoncxx::types::b_date(std::chrono::system_clock::now() + expiresIn))));
  }
}


// Phase 5: the lock of the schema upgrades is a lease
TEST_F(MongoDBIndexTest, Lock)
{
  std::unique_ptr<MongoDBIndex> index;
  std::unique_ptr<DatabaseManager> manager;

  // The lock of an Orthanc that crashed has expired: it is taken at once
  InsertLock(*database_, "crashed", std::chrono::milliseconds(-1000));

  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  manager.reset(StartAnotherIndex(index));
  ASSERT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
  ASSERT_EQ(0, CountDocuments("Locks"));

  // Another Orthanc is upgrading: wait until its lease expires
  InsertLock(*database_, "running", std::chrono::milliseconds(2000));

  start = std::chrono::steady_clock::now();
  manager.reset(StartAnotherIndex(index));
  ASSERT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1000));
  ASSERT_EQ(0, CountDocuments("Locks"));

  // Several Orthanc starting together: one upgrades at a time, all start
  RunConcurrently(4, [&](size_t thread)
  {
    std::unique_ptr<MongoDBIndex> index2;
    std::unique_ptr<DatabaseManager> manager2(StartAnotherIndex(index2));
  });

  ASSERT_EQ(0, CountDocuments("Locks"));
  ASSERT_EQ(std::to_string(MongoDBSchema::SCHEMA_REVISION), GetGlobalProperty(Orthanc::GlobalProperty_DatabasePatchLevel));
}


// Phase 6: "IncrementGlobalProperty()" is atomic, and keeps a string value
TEST_F(MongoDBIndexTest, IncrementGlobalProperty)
{
  ASSERT_TRUE(index_->HasAtomicIncrementGlobalProperty());

  ASSERT_EQ(5, index_->IncrementGlobalProperty(*manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9, 5));
  ASSERT_EQ(3, index_->IncrementGlobalProperty(*manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9, -2));
  ASSERT_EQ("3", GetGlobalProperty(Orthanc::GlobalProperty_DatabaseInternal9));

  // The properties of one server are separate
  ASSERT_EQ(1, index_->IncrementGlobalProperty(*manager_, "some-server", Orthanc::GlobalProperty_DatabaseInternal9, 1));
  ASSERT_EQ(4, index_->IncrementGlobalProperty(*manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal9, 1));

  // A value written by "SetGlobalProperty()" can be incremented
  index_->SetGlobalProperty(*manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8, "41");
  ASSERT_EQ(42, index_->IncrementGlobalProperty(*manager_, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal8, 1));

  // Concurrent increments of a new property: each value is returned once
  const size_t THREADS = 8;
  const size_t INCREMENTS = 25;
  std::vector< std::vector<int64_t> > values(THREADS);

  RunConcurrently(THREADS, [&](size_t thread)
  {
    DatabaseManager manager(index_->CreateDatabaseFactory());
    for (size_t i = 0; i < INCREMENTS; i++)
    {
      values[thread].push_back(index_->IncrementGlobalProperty(
                                 manager, MISSING_SERVER_IDENTIFIER, Orthanc::GlobalProperty_DatabaseInternal7, 1));
    }
  });

  std::set<int64_t> unique;
  for (size_t i = 0; i < THREADS; i++)
  {
    unique.insert(values[i].begin(), values[i].end());
  }

  ASSERT_EQ(THREADS * INCREMENTS, unique.size());
  ASSERT_EQ(1, *unique.begin());
  ASSERT_EQ(static_cast<int64_t>(THREADS * INCREMENTS), *unique.rbegin());
  ASSERT_EQ(1, CountDocuments("GlobalProperties", make_document(
                                kvp("property", static_cast<int32_t>(Orthanc::GlobalProperty_DatabaseInternal7)))));
}


TEST_F(MongoDBIndexTest, MeasureLatency)
{
  ASSERT_TRUE(index_->HasMeasureLatency());
  const uint64_t latency = index_->MeasureLatency(*manager_);
  ASSERT_GT(latency, 0u);
  ASSERT_LT(latency, 10000000u);  // 10 seconds
}


namespace
{
  struct Statistics
  {
    int64_t patients_ = 0;
    int64_t studies_ = 0;
    int64_t series_ = 0;
    int64_t instances_ = 0;
    int64_t compressedSize_ = 0;
    int64_t uncompressedSize_ = 0;

    bool operator== (const Statistics& other) const
    {
      return (patients_ == other.patients_ && studies_ == other.studies_ && series_ == other.series_ &&
              instances_ == other.instances_ && compressedSize_ == other.compressedSize_ &&
              uncompressedSize_ == other.uncompressedSize_);
    }
  };

  std::ostream& operator<< (std::ostream& stream, const Statistics& s)
  {
    return stream << s.patients_ << "/" << s.studies_ << "/" << s.series_ << "/" << s.instances_
                  << " resources, " << s.compressedSize_ << "/" << s.uncompressedSize_ << " bytes";
  }
}


class MongoDBStatisticsTest : public MongoDBIndexTest
{
protected:
  Statistics UpdateAndGet(DatabaseManager& manager)
  {
    Statistics s;
    ApplyWithRetries(manager, [&]()
    {
      index_->UpdateAndGetStatistics(manager, s.patients_, s.studies_, s.series_, s.instances_,
                                     s.compressedSize_, s.uncompressedSize_);
    });
    return s;
  }

  Statistics GetLegacy()
  {
    Statistics s;
    s.patients_ = index_->GetResourcesCount(*manager_, OrthancPluginResourceType_Patient);
    s.studies_ = index_->GetResourcesCount(*manager_, OrthancPluginResourceType_Study);
    s.series_ = index_->GetResourcesCount(*manager_, OrthancPluginResourceType_Series);
    s.instances_ = index_->GetResourcesCount(*manager_, OrthancPluginResourceType_Instance);
    s.compressedSize_ = index_->GetTotalCompressedSize(*manager_);
    s.uncompressedSize_ = index_->GetTotalUncompressedSize(*manager_);
    return s;
  }

  // The values computed from the data, bypassing the plugin
  Statistics Scan()
  {
    Statistics s;
    s.patients_ = CountDocuments("Resources", make_document(kvp("resourceType", 0)));
    s.studies_ = CountDocuments("Resources", make_document(kvp("resourceType", 1)));
    s.series_ = CountDocuments("Resources", make_document(kvp("resourceType", 2)));
    s.instances_ = CountDocuments("Resources", make_document(kvp("resourceType", 3)));

    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    mongocxx::pipeline pipeline;
    pipeline.group(make_document(kvp("_id", bsoncxx::types::b_null()),
                                 kvp("c", make_document(kvp("$sum", "$compressedSize"))),
                                 kvp("u", make_document(kvp("$sum", "$uncompressedSize")))));
    for (const bsoncxx::document::view& d : GetCollection(client, "AttachedFiles").aggregate(pipeline))
    {
      s.compressedSize_ = (d["c"].type() == bsoncxx::type::k_int64 ? d["c"].get_int64().value : d["c"].get_int32().value);
      s.uncompressedSize_ = (d["u"].type() == bsoncxx::type::k_int64 ? d["u"].get_int64().value : d["u"].get_int32().value);
    }

    return s;
  }

  void Housekeeping()
  {
    index_->PerformDbHousekeeping(*manager_);
  }

  // A database of the previous versions: no totals, and no changes
  void RemoveStatistics(bool keepChanges)
  {
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    GetCollection(client, "GlobalProperties").delete_one(
      make_document(kvp("property", static_cast<int32_t>(Orthanc::GlobalProperty_DatabaseInternal0))));

    if (!keepChanges)
    {
      GetCollection(client, "StatisticsChanges").delete_many(make_document());
    }
  }

  bool HasTotals()
  {
    return CountDocuments("GlobalProperties", make_document(
                            kvp("property", static_cast<int32_t>(Orthanc::GlobalProperty_DatabaseInternal0)))) == 1;
  }
};


// Phase 6: the statistics are counters, updated as the data changes (bug 10)
TEST_F(MongoDBStatisticsTest, Counters)
{
  ASSERT_TRUE(index_->HasUpdateAndGetStatistics());
  ASSERT_TRUE(index_->HasPerformDbHousekeeping());

  // A new database gets zero totals at once
  ASSERT_TRUE(HasTotals());
  ASSERT_EQ(Statistics(), UpdateAndGet(*manager_));

  std::vector<int64_t> a = CreateHierarchy("a-");
  std::vector<int64_t> b = CreateHierarchy("b-");
  AddAttachment(a[3], "a1");
  AddAttachment(b[3], "b1");
  AddAttachment(b[2], "b2");

  Statistics expected = Scan();
  ASSERT_EQ(2, expected.patients_);
  ASSERT_EQ(3 * 42, expected.compressedSize_);

  // The pending changes are counted before they are consolidated
  ASSERT_GT(CountDocuments("StatisticsChanges"), 0);
  ASSERT_EQ(expected, GetLegacy());
  ASSERT_EQ(expected, UpdateAndGet(*manager_));
  ASSERT_EQ(0, CountDocuments("StatisticsChanges"));
  ASSERT_EQ(expected, GetLegacy());

  // A cascade delete removes the resources and their attachments
  RecordingOutput output;
  index_->DeleteResource(output, *manager_, b[0]);
  expected = Scan();
  ASSERT_EQ(1, expected.patients_);
  ASSERT_EQ(42, expected.compressedSize_);
  ASSERT_EQ(expected, GetLegacy());

  // The housekeeping consolidates, once Orthanc has started
  Housekeeping();
  ASSERT_GT(CountDocuments("StatisticsChanges"), 0);
  index_->SetOrthancStarted();
  Housekeeping();
  ASSERT_EQ(0, CountDocuments("StatisticsChanges"));
  ASSERT_EQ(expected, UpdateAndGet(*manager_));

  // An attachment of its own, then the rest
  index_->DeleteAttachment(output, *manager_, a[3], OrthancPluginContentType_Dicom);
  index_->DeleteResource(output, *manager_, a[0]);
  ASSERT_EQ(Statistics(), UpdateAndGet(*manager_));
  ASSERT_EQ(Statistics(), Scan());
}


// Phase 6: a database of the previous versions has no totals: they are
// computed by the housekeeping, and the reads are full scans until then
TEST_F(MongoDBStatisticsTest, ComputedByHousekeeping)
{
  for (int i = 0; i < 3; i++)
  {
    std::vector<int64_t> ids = CreateHierarchy("p" + std::to_string(i) + "-");
    AddAttachment(ids[3], "u" + std::to_string(i));
  }

  RemoveStatistics(false);
  const Statistics expected = Scan();
  ASSERT_EQ(3, expected.instances_);

  ASSERT_EQ(expected, GetLegacy());
  ASSERT_EQ(expected, UpdateAndGet(*manager_));
  ASSERT_FALSE(HasTotals());

  // Changes written before the totals are computed are not lost
  std::vector<int64_t> ids = CreateHierarchy("late-");
  AddAttachment(ids[3], "late");
  ASSERT_GT(CountDocuments("StatisticsChanges"), 0);

  Housekeeping();
  ASSERT_FALSE(HasTotals());  // Orthanc has not started

  index_->SetOrthancStarted();
  Housekeeping();
  ASSERT_TRUE(HasTotals());
  ASSERT_EQ(0, CountDocuments("StatisticsChanges"));
  ASSERT_EQ(Scan(), UpdateAndGet(*manager_));
  ASSERT_EQ(4, Scan().instances_);
}


// Phase 6: the totals are computed while changes are pending
TEST_F(MongoDBStatisticsTest, ComputedWithPendingChanges)
{
  std::vector<int64_t> ids = CreateHierarchy("");
  AddAttachment(ids[3], "uuid");

  RemoveStatistics(true /* the changes of the new database stay pending */);
  ASSERT_GT(CountDocuments("StatisticsChanges"), 0);

  index_->SetOrthancStarted();
  Housekeeping();
  ASSERT_TRUE(HasTotals());
  ASSERT_EQ(Scan(), GetLegacy());
  ASSERT_EQ(Scan(), UpdateAndGet(*manager_));
}


// Phase 6: consolidations that run concurrently with writers, and with
// each other, never count a change twice
TEST_F(MongoDBStatisticsTest, ConcurrentConsolidation)
{
  const size_t WRITERS = 4;
  const size_t INSTANCES = 15;
  std::atomic<size_t> done(0);

  RunConcurrently(WRITERS + 2, [&](size_t thread)
  {
    DatabaseManager manager(index_->CreateDatabaseFactory());

    if (thread < WRITERS)
    {
      for (size_t i = 0; i < INSTANCES; i++)
      {
        const std::string prefix = std::to_string(thread) + "-" + std::to_string(i) + "-";
        ApplyWithRetries(manager, [&]()
        {
          OrthancPluginCreateInstanceResult r;
          memset(&r, 0, sizeof(r));
          index_->CreateInstance(r, manager, "patient", (prefix + "study").c_str(),
                                 (prefix + "series").c_str(), (prefix + "instance").c_str());

          if (r.isNewInstance)
          {
            OrthancPluginAttachment attachment;
            attachment.uuid = prefix.c_str();
            attachment.contentType = OrthancPluginContentType_Dicom;
            attachment.uncompressedSize = 100;
            attachment.uncompressedHash = "md5";
            attachment.compressionType = OrthancPluginCompressionType_None;
            attachment.compressedSize = 10;
            attachment.compressedHash = "md5";
            index_->AddAttachment(manager, r.instanceId, attachment, 0);
          }
        });
      }

      done++;
    }
    else
    {
      while (done < WRITERS)
      {
        UpdateAndGet(manager);
      }
    }
  });

  const Statistics expected = Scan();
  ASSERT_EQ(1, expected.patients_);
  ASSERT_EQ(static_cast<int64_t>(WRITERS * INSTANCES), expected.instances_);
  ASSERT_EQ(static_cast<int64_t>(WRITERS * INSTANCES * 10), expected.compressedSize_);
  ASSERT_EQ(expected, UpdateAndGet(*manager_));
  ASSERT_EQ(0, CountDocuments("StatisticsChanges"));
}


// Phase 6: "GetChangesExtended()" filters on a range of sequence
// numbers and on the change types, as the SQL plugins do
TEST_F(MongoDBIndexTest, GetChangesExtended)
{
  ASSERT_TRUE(index_->HasExtendedChanges());

  int64_t p = index_->CreateResource(*manager_, "p", OrthancPluginResourceType_Patient);

  // Changes 1..10, of type 1 for the odd sequence numbers and 2 for the even ones
  for (int i = 1; i <= 10; i++)
  {
    index_->LogChange(*manager_, (i % 2 == 1 ? 1 : 2), p, OrthancPluginResourceType_Patient, "20260924T000000");
  }

  const std::set<uint32_t> all;
  const std::set<uint32_t> even = { 2 };

  {
    // (since, to]
    RecordingOutput output;
    bool done;
    index_->GetChangesExtended(output, done, *manager_, 3, 7, all, 100);
    ASSERT_TRUE(done);
    ASSERT_EQ(std::vector<int64_t>({ 4, 5, 6, 7 }), output.changes_);
    ASSERT_EQ("p", output.changesPublicIds_[0]);
  }

  {
    // The first changes after "since", and whether more are left
    RecordingOutput output;
    bool done;
    index_->GetChangesExtended(output, done, *manager_, 3, -1, all, 2);
    ASSERT_FALSE(done);
    ASSERT_EQ(std::vector<int64_t>({ 4, 5 }), output.changes_);
  }

  {
    // Without "since", the most recent changes up to "to", in ascending order
    RecordingOutput output;
    bool done;
    index_->GetChangesExtended(output, done, *manager_, 0, 8, all, 3);
    ASSERT_FALSE(done);
    ASSERT_EQ(std::vector<int64_t>({ 6, 7, 8 }), output.changes_);
  }

  {
    RecordingOutput output;
    bool done;
    index_->GetChangesExtended(output, done, *manager_, 0, 8, even, 10);
    ASSERT_TRUE(done);
    ASSERT_EQ(std::vector<int64_t>({ 2, 4, 6, 8 }), output.changes_);
  }

  {
    RecordingOutput output;
    bool done;
    index_->GetChangesExtended(output, done, *manager_, 0, -1, even, 2);
    ASSERT_FALSE(done);
    ASSERT_EQ(std::vector<int64_t>({ 2, 4 }), output.changes_);
  }

  {
    // Exactly "limit" changes left: done
    RecordingOutput output;
    bool done;
    index_->GetChangesExtended(output, done, *manager_, 6, -1, even, 2);
    ASSERT_TRUE(done);
    ASSERT_EQ(std::vector<int64_t>({ 8, 10 }), output.changes_);
  }
}


// Phase 6: labels, one document per label of a resource
TEST_F(MongoDBIndexTest, Labels)
{
  ASSERT_TRUE(index_->HasLabelsSupport());

  std::vector<int64_t> ids = CreateHierarchy("");
  index_->AddLabel(*manager_, ids[1], "b");
  index_->AddLabel(*manager_, ids[1], "a");
  index_->AddLabel(*manager_, ids[1], "a");  // Idempotent
  index_->AddLabel(*manager_, ids[2], "c");

  std::list<std::string> labels;
  index_->ListLabels(labels, *manager_, ids[1]);
  ASSERT_EQ(std::list<std::string>({ "a", "b" }), labels);
  ASSERT_EQ(2, CountDocuments("Labels", make_document(kvp("id", ids[1]))));

  index_->ListAllLabels(labels, *manager_);
  ASSERT_EQ(std::list<std::string>({ "a", "b", "c" }), labels);

  index_->RemoveLabel(*manager_, ids[1], "b");
  index_->RemoveLabel(*manager_, ids[1], "missing");
  index_->ListLabels(labels, *manager_, ids[1]);
  ASSERT_EQ(std::list<std::string>({ "a" }), labels);

  // Concurrent additions of the same label
  RunConcurrently(8, [&](size_t thread)
  {
    DatabaseManager manager(index_->CreateDatabaseFactory());
    index_->AddLabel(manager, ids[0], "same");
  });
  ASSERT_EQ(1, CountDocuments("Labels", make_document(kvp("id", ids[0]), kvp("label", "same"))));

  // The labels are deleted with their resources
  RecordingOutput output;
  index_->DeleteResource(output, *manager_, ids[0]);
  ASSERT_EQ(0, CountDocuments("Labels"));
}


namespace
{
  std::set<std::string> LookupLabels(MongoDBIndex& index,
                                     DatabaseManager& manager,
                                     OrthancPluginResourceType level,
                                     const std::set<std::string>& labels,
                                     LabelsConstraint constraint,
                                     const DatabaseConstraints& constraints = DatabaseConstraints())
  {
    RecordingOutput output;
    index.LookupResources(output, manager, constraints, level, labels, constraint, 0, false);
    return output.matches_;
  }
}


// Phase 6: the label constraints of "LookupResources()", alone (the
// labels are then the start of the pipeline) or with tags
TEST_F(MongoDBIndexTest, LookupLabels)
{
  typedef std::set<std::string> Set;

  std::vector<int64_t> s1 = CreateHierarchy("1-");
  std::vector<int64_t> s2 = CreateHierarchy("2-");
  std::vector<int64_t> s3 = CreateHierarchy("3-");
  index_->AddLabel(*manager_, s1[1], "a");
  index_->AddLabel(*manager_, s1[1], "b");
  index_->AddLabel(*manager_, s2[1], "a");
  index_->AddLabel(*manager_, s3[2], "b");  // A series: not a study label

  const OrthancPluginResourceType study = OrthancPluginResourceType_Study;

  ASSERT_EQ(Set({ "1-study", "2-study" }), LookupLabels(*index_, *manager_, study, { "a" }, LabelsConstraint_Any));
  ASSERT_EQ(Set({ "1-study", "2-study" }), LookupLabels(*index_, *manager_, study, { "a", "b" }, LabelsConstraint_Any));
  ASSERT_EQ(Set({ "1-study" }), LookupLabels(*index_, *manager_, study, { "a", "b" }, LabelsConstraint_All));
  ASSERT_EQ(Set({ "1-study" }), LookupLabels(*index_, *manager_, study, { "b" }, LabelsConstraint_Any));
  ASSERT_EQ(Set(), LookupLabels(*index_, *manager_, study, { "missing" }, LabelsConstraint_Any));
  ASSERT_EQ(Set({ "2-study", "3-study" }), LookupLabels(*index_, *manager_, study, { "b" }, LabelsConstraint_None));
  ASSERT_EQ(Set({ "3-study" }), LookupLabels(*index_, *manager_, study, { "a", "b" }, LabelsConstraint_None));

  // Since Orthanc 1.12.11, "None" without labels: the resources without any label
  ASSERT_EQ(Set({ "3-study" }), LookupLabels(*index_, *manager_, study, {}, LabelsConstraint_None));
  ASSERT_EQ(Set({ "1-series", "2-series" }),
            LookupLabels(*index_, *manager_, OrthancPluginResourceType_Series, {}, LabelsConstraint_None));

  // No labels and "All" or "Any": no label constraint
  ASSERT_EQ(3u, LookupLabels(*index_, *manager_, study, {}, LabelsConstraint_All).size());

  // With a tag constraint, which then starts the pipeline
  index_->SetMainDicomTag(*manager_, s1[1], 0x0008, 0x1030, "CT");
  index_->SetMainDicomTag(*manager_, s2[1], 0x0008, 0x1030, "CT");
  index_->SetMainDicomTag(*manager_, s3[1], 0x0008, 0x1030, "CT");

  DatabaseConstraints ct;
  ct.AddConstraint(CreateConstraint(Orthanc::ResourceType_Study, 0x0008, 0x1030, false, ConstraintType_Equal, "CT", true));

  ASSERT_EQ(Set({ "1-study" }), LookupLabels(*index_, *manager_, study, { "a", "b" }, LabelsConstraint_All, ct));
  ASSERT_EQ(Set({ "2-study", "3-study" }), LookupLabels(*index_, *manager_, study, { "b" }, LabelsConstraint_None, ct));
  ASSERT_EQ(Set({ "3-study" }), LookupLabels(*index_, *manager_, study, {}, LabelsConstraint_None, ct));

  // Instances, labelled through their series? No: labels are per resource
  ASSERT_EQ(Set(), LookupLabels(*index_, *manager_, OrthancPluginResourceType_Instance, { "b" }, LabelsConstraint_Any));
}


// Phase 6: concurrent consumers of a queue get each value exactly once,
// whether they pop it or reserve then acknowledge it
TEST_F(MongoDBIndexTest, ConcurrentQueue)
{
  ASSERT_TRUE(index_->HasQueues());
  ASSERT_TRUE(index_->HasReserveQueueValue());

  const size_t VALUES = 200;
  const size_t THREADS = 8;

  for (size_t i = 0; i < VALUES; i++)
  {
    std::string value = "v" + std::to_string(i);
    value.push_back('\0');  // Binary values
    index_->EnqueueValue(*manager_, "queue", value);
  }

  ASSERT_EQ(VALUES, index_->GetQueueSize(*manager_, "queue"));

  std::vector< std::vector<std::string> > received(THREADS);

  RunConcurrently(THREADS, [&](size_t thread)
  {
    DatabaseManager manager(index_->CreateDatabaseFactory());

    for (;;)
    {
      std::string value;
      bool found = false;

      // Half of the consumers pop, the other half reserve and acknowledge
      ApplyWithRetries(manager, [&]()
      {
        if (thread % 2 == 0)
        {
          found = index_->DequeueValue(value, manager, "queue", true);
        }
        else
        {
          uint64_t id;
          found = index_->ReserveQueueValue(value, id, manager, "queue", thread % 4 == 1, 60);
          if (found)
          {
            index_->AcknowledgeQueueValue(manager, "queue", id);
          }
        }
      });

      if (found)
      {
        received[thread].push_back(value);
      }
      else
      {
        break;
      }
    }
  });

  std::set<std::string> unique;
  size_t total = 0;
  for (size_t i = 0; i < THREADS; i++)
  {
    unique.insert(received[i].begin(), received[i].end());
    total += received[i].size();
  }

  ASSERT_EQ(VALUES, total);
  ASSERT_EQ(VALUES, unique.size());
  ASSERT_EQ(0u, index_->GetQueueSize(*manager_, "queue"));
}


// Phase 6: a reserved value is skipped by the other consumers, and is
// counted in the size of its queue
TEST_F(MongoDBIndexTest, QueueReservation)
{
  index_->EnqueueValue(*manager_, "queue", "a");
  index_->EnqueueValue(*manager_, "queue", "b");

  std::string value;
  uint64_t id;
  ASSERT_TRUE(index_->ReserveQueueValue(value, id, *manager_, "queue", true, 60));
  ASSERT_EQ("a", value);
  ASSERT_EQ(2u, index_->GetQueueSize(*manager_, "queue"));

  // "a" is reserved: the front of the queue is "b"
  ASSERT_TRUE(index_->DequeueValue(value, *manager_, "queue", true));
  ASSERT_EQ("b", value);
  ASSERT_FALSE(index_->DequeueValue(value, *manager_, "queue", true));
  ASSERT_FALSE(index_->ReserveQueueValue(value, id, *manager_, "queue", false, 60));

  // Another queue with the same ID of value cannot acknowledge it
  ASSERT_THROW(index_->AcknowledgeQueueValue(*manager_, "other", id), Orthanc::OrthancException);
  index_->AcknowledgeQueueValue(*manager_, "queue", id);
  ASSERT_EQ(0u, index_->GetQueueSize(*manager_, "queue"));
  ASSERT_THROW(index_->AcknowledgeQueueValue(*manager_, "queue", id), Orthanc::OrthancException);
}


// Phase 6: the custom data of the attachments, in the optional field "customData"
TEST_F(MongoDBIndexTest, AttachmentCustomData)
{
  ASSERT_TRUE(index_->HasAttachmentCustomDataSupport());

  std::vector<int64_t> ids = CreateHierarchy("");

  std::string binary = "custom";
  binary.push_back('\0');
  binary += "data";

  OrthancPluginAttachment attachment;
  attachment.uuid = "with";
  attachment.contentType = OrthancPluginContentType_Dicom;
  attachment.uncompressedSize = 1;
  attachment.uncompressedHash = "md5";
  attachment.compressionType = OrthancPluginCompressionType_None;
  attachment.compressedSize = 1;
  attachment.compressedHash = "md5";
  index_->AddAttachment(*manager_, ids[3], attachment, 0, binary);

  AddAttachment(ids[2], "without");  // Same document shape as the previous versions
  ASSERT_EQ(0, CountDocuments("AttachedFiles", make_document(kvp("uuid", "without"),
                                                             kvp("customData", make_document(kvp("$exists", true))))));

  std::string s;
  index_->GetAttachmentCustomData(s, *manager_, "with");
  ASSERT_EQ(binary, s);
  index_->GetAttachmentCustomData(s, *manager_, "without");
  ASSERT_TRUE(s.empty());
  ASSERT_THROW(index_->GetAttachmentCustomData(s, *manager_, "nope"), Orthanc::OrthancException);

  index_->SetAttachmentCustomData(*manager_, "without", "later");
  index_->GetAttachmentCustomData(s, *manager_, "without");
  ASSERT_EQ("later", s);

  {
    RecordingOutput output;
    int64_t revision;
    ASSERT_TRUE(index_->LookupAttachment(output, revision, *manager_, ids[3], OrthancPluginContentType_Dicom));
    ASSERT_EQ(binary, output.customData_["with"]);
  }

  // Clearing it removes the field
  index_->SetAttachmentCustomData(*manager_, "without", "");
  index_->GetAttachmentCustomData(s, *manager_, "without");
  ASSERT_TRUE(s.empty());
  ASSERT_EQ(0, CountDocuments("AttachedFiles", make_document(kvp("customData", make_document(kvp("$exists", true))),
                                                             kvp("uuid", "without"))));

  // Orthanc gets the custom data of the deleted attachments, e.g. to delete their files
  RecordingOutput output;
  index_->DeleteResource(output, *manager_, ids[0]);
  ASSERT_EQ(binary, output.customData_["with"]);
  ASSERT_TRUE(output.customData_["without"].empty());
}


// Phase 6: the audit logs, with the filters of the REST route "/plugins/mongodb/audit-logs"
TEST_F(MongoDBIndexTest, AuditLogs)
{
  ASSERT_TRUE(index_->HasAuditLogs());

  std::string binary = "{\"a\":1}";
  binary.push_back('\0');

  index_->RecordAuditLog(*manager_, "plugin", "alice", OrthancPluginResourceType_Study, "s1", "view", binary.c_str(), binary.size());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  index_->RecordAuditLog(*manager_, "plugin", "bob", OrthancPluginResourceType_Study, "s1", "delete", NULL, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  index_->RecordAuditLog(*manager_, "other", "alice", OrthancPluginResourceType_Patient, "p1", "view", NULL, 0);

  std::list<IDatabaseBackend::AuditLog> logs;
  index_->GetAuditLogs(*manager_, logs, "", "", "", "", "", 0, 0);
  ASSERT_EQ(3u, logs.size());

  std::vector<IDatabaseBackend::AuditLog> all(logs.begin(), logs.end());
  ASSERT_EQ("alice", all[0].GetUserId());
  ASSERT_EQ("bob", all[1].GetUserId());
  ASSERT_EQ("other", all[2].GetSourcePlugin());
  ASSERT_EQ(OrthancPluginResourceType_Patient, all[2].GetResourceType());
  ASSERT_EQ("p1", all[2].GetResourceId());
  ASSERT_TRUE(all[0].HasLogData());
  ASSERT_EQ(binary, all[0].GetLogData());
  ASSERT_FALSE(all[1].HasLogData());

  // "2026-09-24T10:00:00.123Z"
  const std::string& ts = all[1].GetTimestamp();
  ASSERT_EQ(24u, ts.size());
  ASSERT_EQ('T', ts[10]);
  ASSERT_EQ('.', ts[19]);
  ASSERT_EQ('Z', ts[23]);
  ASSERT_LT(all[0].GetTimestamp(), all[1].GetTimestamp());

  index_->GetAuditLogs(*manager_, logs, "alice", "", "", "", "", 0, 0);
  ASSERT_EQ(2u, logs.size());
  index_->GetAuditLogs(*manager_, logs, "", "s1", "", "", "", 0, 0);
  ASSERT_EQ(2u, logs.size());
  index_->GetAuditLogs(*manager_, logs, "alice", "", "view", "", "", 0, 0);
  ASSERT_EQ(2u, logs.size());
  index_->GetAuditLogs(*manager_, logs, "", "", "delete", "", "", 0, 0);
  ASSERT_EQ(1u, logs.size());
  ASSERT_EQ("bob", logs.front().GetUserId());

  // "since" skips logs, "limit" 0 is no limit
  index_->GetAuditLogs(*manager_, logs, "", "", "", "", "", 1, 1);
  ASSERT_EQ(1u, logs.size());
  ASSERT_EQ("bob", logs.front().GetUserId());
  index_->GetAuditLogs(*manager_, logs, "", "", "", "", "", 1, 0);
  ASSERT_EQ(2u, logs.size());

  // [from, to[, from the timestamps that the route returns
  index_->GetAuditLogs(*manager_, logs, "", "", "", all[1].GetTimestamp(), "", 0, 0);
  ASSERT_EQ(2u, logs.size());
  index_->GetAuditLogs(*manager_, logs, "", "", "", "", all[1].GetTimestamp(), 0, 0);
  ASSERT_EQ(1u, logs.size());
  ASSERT_EQ("alice", logs.front().GetUserId());
  index_->GetAuditLogs(*manager_, logs, "", "", "", all[0].GetTimestamp(), all[2].GetTimestamp(), 0, 0);
  ASSERT_EQ(2u, logs.size());

  // Other ISO 8601 forms, as with "::TIMESTAMPTZ"
  index_->GetAuditLogs(*manager_, logs, "", "", "", "2000-01-01", "", 0, 0);
  ASSERT_EQ(3u, logs.size());
  index_->GetAuditLogs(*manager_, logs, "", "", "", "2000-01-01T00:00:00+02:00", "2001-01-01T00:00:00Z", 0, 0);
  ASSERT_EQ(0u, logs.size());

  ASSERT_THROW(index_->GetAuditLogs(*manager_, logs, "", "", "", "yesterday", "", 0, 0), Orthanc::OrthancException);
}


// Phase 6: "AuditLogsRetentionDays" turns the index on "ts" into a TTL index
TEST_F(MongoDBIndexTest, AuditLogsRetention)
{
  auto getTtl = [&]() -> int64_t
  {
    mongocxx::client client{mongocxx::uri{database_->GetUri()}};
    for (const bsoncxx::document::view& index : GetCollection(client, "AuditLogs").list_indexes())
    {
      if (std::string(index["name"].get_string().value) == "ts_1")
      {
        bsoncxx::document::element ttl = index["expireAfterSeconds"];
        if (!ttl)
        {
          return 0;
        }

        return (ttl.type() == bsoncxx::type::k_int32 ? ttl.get_int32().value : ttl.get_int64().value);
      }
    }

    return -1;  // No index
  };

  ASSERT_EQ(0, getTtl());  // Off by default

  MongoDBParameters parameters = CreateTestParameters(database_->GetUri());
  std::unique_ptr<MongoDBIndex> index;
  std::unique_ptr<DatabaseManager> manager;

  parameters.SetAuditLogsRetentionDays(30);
  manager.reset(StartAnotherIndex(index, parameters));
  ASSERT_EQ(30 * 24 * 3600, getTtl());

  parameters.SetAuditLogsRetentionDays(7);
  manager.reset(StartAnotherIndex(index, parameters));
  ASSERT_EQ(7 * 24 * 3600, getTtl());

  parameters.SetAuditLogsRetentionDays(0);
  manager.reset(StartAnotherIndex(index, parameters));
  ASSERT_EQ(0, getTtl());

  std::list<std::string> missing;
  ASSERT_TRUE(MongoDBSchema::CheckIndexes(missing, *manager));
}


namespace
{
  namespace Messages = Orthanc::DatabasePluginMessages;

  static const uint16_t STUDY_DATE = 0x0020;           // (0008,0020), study

  static const int32_t METADATA_TYPE = 1024;           // A user-defined metadata


  /**
   * The dataset of the Find tests:
   *
   *   pa (DOE)   - sa1 (CT head, 20200101) - sa1-1 (CT) - sa1-1-i1 (1), sa1-1-i2 (9), sa1-1-i3 (10), sa1-1-i4 (none)
   *                                        - sa1-2 (SR) - sa1-2-i1 (1)
   *              - sa2 (MR, 20210101)      - sa2-1 (MR) - sa2-1-i1 (1)
   *   pb (SMITH) - sb1 (CT chest, 20190101) - sb1-1 (CT) - sb1-1-i1 (1)
   *
   * The studies also have the "PatientName" of their patient, as in Orthanc.
   **/
  class MongoDBFindTest : public MongoDBIndexTest
  {
  protected:
    std::map<std::string, int64_t>  ids_;

    int64_t Create(const std::string& publicId,
                   OrthancPluginResourceType type,
                   const std::string& parent)
    {
      int64_t id = CreateChild(publicId, type, parent.empty() ? -1 : ids_[parent]);
      ids_[publicId] = id;
      return id;
    }

    void SetTag(const std::string& resource,
                uint16_t group,
                uint16_t element,
                const std::string& value)
    {
      index_->SetMainDicomTag(*manager_, ids_[resource], group, element, value.c_str());
    }

    virtual void SetUp() ORTHANC_OVERRIDE
    {
      MongoDBIndexTest::SetUp();

      Create("pa", OrthancPluginResourceType_Patient, "");
      Create("pb", OrthancPluginResourceType_Patient, "");
      Create("sa1", OrthancPluginResourceType_Study, "pa");
      Create("sa2", OrthancPluginResourceType_Study, "pa");
      Create("sb1", OrthancPluginResourceType_Study, "pb");
      Create("sa1-1", OrthancPluginResourceType_Series, "sa1");
      Create("sa1-2", OrthancPluginResourceType_Series, "sa1");
      Create("sa2-1", OrthancPluginResourceType_Series, "sa2");
      Create("sb1-1", OrthancPluginResourceType_Series, "sb1");
      Create("sa1-1-i1", OrthancPluginResourceType_Instance, "sa1-1");
      Create("sa1-1-i2", OrthancPluginResourceType_Instance, "sa1-1");
      Create("sa1-1-i3", OrthancPluginResourceType_Instance, "sa1-1");
      Create("sa1-1-i4", OrthancPluginResourceType_Instance, "sa1-1");
      Create("sa1-2-i1", OrthancPluginResourceType_Instance, "sa1-2");
      Create("sa2-1-i1", OrthancPluginResourceType_Instance, "sa2-1");
      Create("sb1-1-i1", OrthancPluginResourceType_Instance, "sb1-1");

      SetTag("pa", GROUP_0010, PATIENT_NAME, "DOE");
      SetTag("pb", GROUP_0010, PATIENT_NAME, "SMITH");
      SetTag("sa1", GROUP_0010, PATIENT_NAME, "DOE");
      SetTag("sa2", GROUP_0010, PATIENT_NAME, "DOE");
      SetTag("sb1", GROUP_0010, PATIENT_NAME, "SMITH");
      SetTag("sa1", GROUP_0008, STUDY_DESCRIPTION, "CT head");
      SetTag("sa2", GROUP_0008, STUDY_DESCRIPTION, "MR");
      SetTag("sb1", GROUP_0008, STUDY_DESCRIPTION, "CT chest");
      SetTag("sa1", GROUP_0008, STUDY_DATE, "20200101");
      SetTag("sa2", GROUP_0008, STUDY_DATE, "20210101");
      SetTag("sb1", GROUP_0008, STUDY_DATE, "20190101");
      SetTag("sa1-1", GROUP_0008, MODALITY, "CT");
      SetTag("sa1-2", GROUP_0008, MODALITY, "SR");
      SetTag("sa2-1", GROUP_0008, MODALITY, "MR");
      SetTag("sb1-1", GROUP_0008, MODALITY, "CT");
      SetTag("sa1-1-i1", GROUP_0020, INSTANCE_NUMBER, "1");
      SetTag("sa1-1-i2", GROUP_0020, INSTANCE_NUMBER, "9");
      SetTag("sa1-1-i3", GROUP_0020, INSTANCE_NUMBER, "10");
      SetTag("sa1-2-i1", GROUP_0020, INSTANCE_NUMBER, "1");
      SetTag("sa2-1-i1", GROUP_0020, INSTANCE_NUMBER, "1");
      SetTag("sb1-1-i1", GROUP_0020, INSTANCE_NUMBER, "1");
    }

    static Messages::Find_Request CreateRequest(Messages::ResourceType level)
    {
      Messages::Find_Request request;
      request.set_level(level);
      return request;
    }

    static void AddTagConstraint(Messages::Find_Request& request,
                                 Messages::ResourceType level,
                                 uint16_t group,
                                 uint16_t element,
                                 Messages::ConstraintType type,
                                 const std::string& value,
                                 bool caseSensitive = true,
                                 bool mandatory = true)
    {
      Messages::DatabaseConstraint* constraint = request.add_dicom_tag_constraints();
      constraint->set_level(level);
      constraint->set_tag_group(group);
      constraint->set_tag_element(element);
      constraint->set_is_identifier_tag(false);
      constraint->set_is_case_sensitive(caseSensitive);
      constraint->set_is_mandatory(mandatory);
      constraint->set_type(type);
      constraint->add_values(value);
    }

    static void AddTagOrdering(Messages::Find_Request& request,
                               Messages::ResourceType level,
                               uint16_t group,
                               uint16_t element,
                               Messages::OrderingDirection direction,
                               Messages::OrderingCast cast = Messages::ORDERING_CAST_STRING)
    {
      Messages::Find_Request_Ordering* ordering = request.add_ordering();
      ordering->set_key_type(Messages::ORDERING_KEY_TYPE_DICOM_TAG);
      ordering->set_direction(direction);
      ordering->set_cast(cast);
      ordering->set_tag_group(group);
      ordering->set_tag_element(element);
      ordering->set_is_identifier_tag(false);
      ordering->set_tag_level(level);
    }

    Messages::TransactionResponse Find(const Messages::Find_Request& request)
    {
      Messages::TransactionResponse response;
      index_->ExecuteFind(response, *manager_, request);
      return response;
    }

    // The public IDs of the answers, in their order
    std::vector<std::string> FindIds(const Messages::Find_Request& request)
    {
      Messages::TransactionResponse response = Find(request);

      std::vector<std::string> ids;
      for (int i = 0; i < response.find_size(); i++)
      {
        ids.push_back(response.find(i).public_id());
        EXPECT_EQ(ids_[response.find(i).public_id()], response.find(i).internal_id());
      }

      return ids;
    }

    uint64_t Count(const Messages::Find_Request& request)
    {
      Messages::TransactionResponse response;
      index_->ExecuteCount(response, *manager_, request);
      return response.count_resources().count();
    }
  };


  typedef std::vector<std::string>  Ids;


  // The values of the tag (group, element) in "tags", in their order
  template <typename Tags>
  std::vector<std::string> GetTagValues(const Tags& tags,
                                        uint16_t group,
                                        uint16_t element)
  {
    std::vector<std::string> values;
    for (int i = 0; i < tags.size(); i++)
    {
      if (tags.Get(i).group() == group &&
          tags.Get(i).element() == element)
      {
        values.push_back(tags.Get(i).value());
      }
    }

    return values;
  }


  template <typename Metadata>
  std::string GetMetadataValue(const Metadata& metadata,
                               int32_t key,
                               int64_t* revision = NULL)
  {
    for (int i = 0; i < metadata.size(); i++)
    {
      if (metadata.Get(i).key() == key)
      {
        if (revision != NULL)
        {
          *revision = metadata.Get(i).revision();
        }

        return metadata.Get(i).value();
      }
    }

    return "(missing)";
  }
}


// Without ordering, the answers are sorted by public ID, which makes the limits repeatable
TEST_F(MongoDBFindTest, FindLevelsAndLimits)
{
  ASSERT_EQ(Ids({"pa", "pb"}), FindIds(CreateRequest(Messages::RESOURCE_PATIENT)));
  ASSERT_EQ(Ids({"sa1", "sa2", "sb1"}), FindIds(CreateRequest(Messages::RESOURCE_STUDY)));
  ASSERT_EQ(Ids({"sa1-1", "sa1-2", "sa2-1", "sb1-1"}), FindIds(CreateRequest(Messages::RESOURCE_SERIES)));
  ASSERT_EQ(7u, FindIds(CreateRequest(Messages::RESOURCE_INSTANCE)).size());
  ASSERT_EQ(7u, Count(CreateRequest(Messages::RESOURCE_INSTANCE)));

  Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
  request.mutable_limits()->set_since(1);
  request.mutable_limits()->set_count(1);
  ASSERT_EQ(Ids({"sa2"}), FindIds(request));
  ASSERT_EQ(1u, Count(request));

  request.mutable_limits()->set_since(2);
  request.mutable_limits()->set_count(5);
  ASSERT_EQ(Ids({"sb1"}), FindIds(request));
  ASSERT_EQ(1u, Count(request));

  request.mutable_limits()->set_since(0);
  request.mutable_limits()->set_count(2);
  ASSERT_EQ(Ids({"sa1", "sa2"}), FindIds(request));

  request.mutable_limits()->set_since(1);
  request.mutable_limits()->set_count(0);  // No limit on the count
  ASSERT_EQ(Ids({"sa2", "sb1"}), FindIds(request));
  ASSERT_EQ(2u, Count(request));

  request.mutable_limits()->set_since(10);
  ASSERT_TRUE(FindIds(request).empty());
  ASSERT_EQ(0u, Count(request));
}


TEST_F(MongoDBFindTest, FindTagConstraints)
{
  {
    // Ancestor level
    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_SERIES);
    AddTagConstraint(request, Messages::RESOURCE_PATIENT, GROUP_0010, PATIENT_NAME, Messages::CONSTRAINT_EQUAL, "doe", false);
    ASSERT_EQ(Ids({"sa1-1", "sa1-2", "sa2-1"}), FindIds(request));
    ASSERT_EQ(3u, Count(request));
  }

  {
    // Child level, which drives the pipeline
    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
    AddTagConstraint(request, Messages::RESOURCE_SERIES, GROUP_0008, MODALITY, Messages::CONSTRAINT_EQUAL, "CT");
    ASSERT_EQ(Ids({"sa1", "sb1"}), FindIds(request));

    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::CONSTRAINT_WILDCARD, "*head");
    ASSERT_EQ(Ids({"sa1"}), FindIds(request));
    ASSERT_EQ(1u, Count(request));
  }

  {
    // Range on the same tag
    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::CONSTRAINT_GREATER_OR_EQUAL, "20191231");
    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::CONSTRAINT_SMALLER_OR_EQUAL, "20201231");
    ASSERT_EQ(Ids({"sa1"}), FindIds(request));
  }

  {
    // Nothing matches (the driver count is 0)
    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::CONSTRAINT_EQUAL, "PET");
    ASSERT_TRUE(FindIds(request).empty());
    ASSERT_EQ(0u, Count(request));
  }

  {
    // Exact match ignoring the case: bounded by the upper and lower-case forms, or not if the value has an "s"
    const char* const descriptions[] = { "MR head", "mr HEAD", "MR hEaD", "MR HEADS", "MR HEA", "US Scan", "us scan", "US SCANS" };
    for (size_t i = 0; i < sizeof(descriptions) / sizeof(descriptions[0]); i++)
    {
      const std::string study = "sc" + std::to_string(i);
      Create(study, OrthancPluginResourceType_Study, "pb");
      SetTag(study, GROUP_0008, STUDY_DESCRIPTION, descriptions[i]);
    }

    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::CONSTRAINT_EQUAL, "Mr Head", false);
    ASSERT_EQ(Ids({"sc0", "sc1", "sc2"}), FindIds(request));
    ASSERT_EQ(3u, Count(request));

    request = CreateRequest(Messages::RESOURCE_STUDY);
    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::CONSTRAINT_EQUAL, "Us Scan", false);
    ASSERT_EQ(Ids({"sc5", "sc6"}), FindIds(request));
    ASSERT_EQ(2u, Count(request));
  }
}


TEST_F(MongoDBFindTest, FindMetadataConstraints)
{
  index_->SetMetadata(*manager_, ids_["sa1"], METADATA_TYPE, "x", 0);
  index_->SetMetadata(*manager_, ids_["sa2"], METADATA_TYPE, "Y", 0);
  index_->SetMetadata(*manager_, ids_["sa1-1"], METADATA_TYPE, "Y", 0);  // Other level

  Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
  Messages::DatabaseMetadataConstraint* constraint = request.add_metadata_constraints();
  constraint->set_metadata(METADATA_TYPE);
  constraint->set_is_case_sensitive(true);
  constraint->set_is_mandatory(true);
  constraint->set_type(Messages::CONSTRAINT_EQUAL);
  constraint->add_values("Y");
  ASSERT_EQ(Ids({"sa2"}), FindIds(request));

  constraint->set_is_case_sensitive(false);
  constraint->set_type(Messages::CONSTRAINT_LIST);
  constraint->add_values("X");
  ASSERT_EQ(Ids({"sa1", "sa2"}), FindIds(request));

  // Not mandatory: a missing metadata matches too
  constraint->set_is_mandatory(false);
  constraint->clear_values();
  constraint->add_values("y");
  ASSERT_EQ(Ids({"sa2", "sb1"}), FindIds(request));
  ASSERT_EQ(2u, Count(request));

  // Combined with a tag constraint
  AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::CONSTRAINT_WILDCARD, "CT*");
  ASSERT_EQ(Ids({"sb1"}), FindIds(request));
}


// The Orthanc ID of the query level or of an ancestor
TEST_F(MongoDBFindTest, FindOrthancIdentifiers)
{
  Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
  request.set_orthanc_id_patient("pa");
  ASSERT_EQ(Ids({"sa1", "sa2"}), FindIds(request));
  ASSERT_EQ(2u, Count(request));

  request.set_orthanc_id_study("sa2");  // The lowest level wins
  ASSERT_EQ(Ids({"sa2"}), FindIds(request));
  ASSERT_EQ(1u, Count(request));

  request.set_orthanc_id_study("pb");  // Not a study
  ASSERT_TRUE(FindIds(request).empty());
  ASSERT_EQ(0u, Count(request));

  request.set_orthanc_id_study("nope");
  ASSERT_TRUE(FindIds(request).empty());

  request = CreateRequest(Messages::RESOURCE_INSTANCE);
  request.set_orthanc_id_series("sa1-1");
  ASSERT_EQ(Ids({"sa1-1-i1", "sa1-1-i2", "sa1-1-i3", "sa1-1-i4"}), FindIds(request));

  // With a tag constraint
  AddTagConstraint(request, Messages::RESOURCE_INSTANCE, GROUP_0020, INSTANCE_NUMBER, Messages::CONSTRAINT_EQUAL, "9");
  ASSERT_EQ(Ids({"sa1-1-i2"}), FindIds(request));

  // With labels
  index_->AddLabel(*manager_, ids_["sa1"], "a");
  request = CreateRequest(Messages::RESOURCE_STUDY);
  request.set_orthanc_id_patient("pa");
  request.add_labels("a");
  request.set_labels_constraint(Messages::LABELS_CONSTRAINT_NONE);
  ASSERT_EQ(Ids({"sa2"}), FindIds(request));
}


TEST_F(MongoDBFindTest, FindOrdering)
{
  Messages::Find_Request request = CreateRequest(Messages::RESOURCE_INSTANCE);
  request.set_orthanc_id_series("sa1-1");

  // A missing value comes last, in both directions
  AddTagOrdering(request, Messages::RESOURCE_INSTANCE, GROUP_0020, INSTANCE_NUMBER, Messages::ORDERING_DIRECTION_ASC, Messages::ORDERING_CAST_INT);
  ASSERT_EQ(Ids({"sa1-1-i1", "sa1-1-i2", "sa1-1-i3", "sa1-1-i4"}), FindIds(request));

  request.mutable_ordering(0)->set_direction(Messages::ORDERING_DIRECTION_DESC);
  ASSERT_EQ(Ids({"sa1-1-i3", "sa1-1-i2", "sa1-1-i1", "sa1-1-i4"}), FindIds(request));

  request.mutable_ordering(0)->set_cast(Messages::ORDERING_CAST_FLOAT);
  ASSERT_EQ(Ids({"sa1-1-i3", "sa1-1-i2", "sa1-1-i1", "sa1-1-i4"}), FindIds(request));

  request.mutable_ordering(0)->set_cast(Messages::ORDERING_CAST_STRING);
  ASSERT_EQ(Ids({"sa1-1-i2", "sa1-1-i3", "sa1-1-i1", "sa1-1-i4"}), FindIds(request));

  // The limits apply after the ordering
  request.mutable_limits()->set_since(1);
  request.mutable_limits()->set_count(2);
  ASSERT_EQ(Ids({"sa1-1-i3", "sa1-1-i1"}), FindIds(request));

  // Ordering by a study tag
  request = CreateRequest(Messages::RESOURCE_STUDY);
  AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::ORDERING_DIRECTION_DESC);
  ASSERT_EQ(Ids({"sa2", "sa1", "sb1"}), FindIds(request));

  // The patient tags are read at the study level, and the public ID breaks the ties
  request = CreateRequest(Messages::RESOURCE_STUDY);
  AddTagOrdering(request, Messages::RESOURCE_PATIENT, GROUP_0010, PATIENT_NAME, Messages::ORDERING_DIRECTION_DESC);
  ASSERT_EQ(Ids({"sb1", "sa1", "sa2"}), FindIds(request));

  // Then by the study date
  AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::ORDERING_DIRECTION_DESC);
  ASSERT_EQ(Ids({"sb1", "sa2", "sa1"}), FindIds(request));

  // Ordering series by a tag of their patient, then of their study
  request = CreateRequest(Messages::RESOURCE_SERIES);
  AddTagOrdering(request, Messages::RESOURCE_PATIENT, GROUP_0010, PATIENT_NAME, Messages::ORDERING_DIRECTION_DESC);
  AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::ORDERING_DIRECTION_ASC);
  ASSERT_EQ(Ids({"sb1-1", "sa1-1", "sa1-2", "sa2-1"}), FindIds(request));

  // Ordering by a metadata, with a constraint on a child level (the driver)
  index_->SetMetadata(*manager_, ids_["sa1"], METADATA_TYPE, "2", 0);
  index_->SetMetadata(*manager_, ids_["sb1"], METADATA_TYPE, "1", 0);

  request = CreateRequest(Messages::RESOURCE_STUDY);
  AddTagConstraint(request, Messages::RESOURCE_SERIES, GROUP_0008, MODALITY, Messages::CONSTRAINT_EQUAL, "CT");
  Messages::Find_Request_Ordering* ordering = request.add_ordering();
  ordering->set_key_type(Messages::ORDERING_KEY_TYPE_METADATA);
  ordering->set_metadata(METADATA_TYPE);
  ordering->set_direction(Messages::ORDERING_DIRECTION_ASC);
  ordering->set_cast(Messages::ORDERING_CAST_INT);
  ASSERT_EQ(Ids({"sb1", "sa1"}), FindIds(request));

  request.clear_dicom_tag_constraints();
  ASSERT_EQ(Ids({"sb1", "sa1", "sa2"}), FindIds(request));
}


TEST_F(MongoDBFindTest, FindResourceContent)
{
  const int64_t study = ids_["sa1"];

  index_->SetMetadata(*manager_, study, METADATA_TYPE, "study", 4);
  index_->SetMetadata(*manager_, ids_["pa"], METADATA_TYPE, "patient", 0);
  index_->AddLabel(*manager_, study, "b");
  index_->AddLabel(*manager_, study, "a");

  OrthancPluginAttachment attachment;
  attachment.uuid = "uuid-study";
  attachment.contentType = OrthancPluginContentType_DicomAsJson;
  attachment.uncompressedSize = 42;
  attachment.uncompressedHash = "md5-u";
  attachment.compressionType = OrthancPluginCompressionType_ZlibWithSize;
  attachment.compressedSize = 21;
  attachment.compressedHash = "md5-c";
  index_->AddAttachment(*manager_, study, attachment, 3, "custom");

  Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
  request.set_orthanc_id_study("sa1");
  request.set_retrieve_main_dicom_tags(true);
  request.set_retrieve_metadata(true);
  request.set_retrieve_labels(true);
  request.set_retrieve_attachments(true);
  request.set_retrieve_parent_identifier(true);
  request.mutable_parent_patient()->set_retrieve_main_dicom_tags(true);
  request.mutable_parent_patient()->set_retrieve_metadata(true);

  Messages::TransactionResponse response = Find(request);
  ASSERT_EQ(1, response.find_size());

  const Messages::Find_Response& found = response.find(0);
  ASSERT_EQ(study, found.internal_id());
  ASSERT_EQ("sa1", found.public_id());
  ASSERT_EQ("pa", found.parent_public_id());

  ASSERT_EQ(3, found.study_content().main_dicom_tags_size());
  ASSERT_EQ(Ids({"CT head"}), GetTagValues(found.study_content().main_dicom_tags(), GROUP_0008, STUDY_DESCRIPTION));

  int64_t revision = -1;
  ASSERT_EQ(1, found.study_content().metadata_size());
  ASSERT_EQ("study", GetMetadataValue(found.study_content().metadata(), METADATA_TYPE, &revision));
  ASSERT_EQ(4, revision);

  ASSERT_EQ(Ids({"DOE"}), GetTagValues(found.patient_content().main_dicom_tags(), GROUP_0010, PATIENT_NAME));
  ASSERT_EQ("patient", GetMetadataValue(found.patient_content().metadata(), METADATA_TYPE));

  std::set<std::string> labels(found.labels().begin(), found.labels().end());
  ASSERT_EQ(MakeSet("a", "b"), labels);

  ASSERT_EQ(1, found.attachments_size());
  ASSERT_EQ(1, found.attachments_revisions_size());
  ASSERT_EQ(3, found.attachments_revisions(0));
  ASSERT_EQ("uuid-study", found.attachments(0).uuid());
  ASSERT_EQ(OrthancPluginContentType_DicomAsJson, found.attachments(0).content_type());
  ASSERT_EQ(42u, found.attachments(0).uncompressed_size());
  ASSERT_EQ("md5-u", found.attachments(0).uncompressed_hash());
  ASSERT_EQ(OrthancPluginCompressionType_ZlibWithSize, found.attachments(0).compression_type());
  ASSERT_EQ(21u, found.attachments(0).compressed_size());
  ASSERT_EQ("md5-c", found.attachments(0).compressed_hash());
  ASSERT_EQ("custom", found.attachments(0).custom_data());

  ASSERT_FALSE(found.has_series_content());
  ASSERT_FALSE(found.has_children_series_content());
  ASSERT_TRUE(found.one_instance_public_id().empty());
}


TEST_F(MongoDBFindTest, FindChildrenContent)
{
  index_->SetMetadata(*manager_, ids_["sa1-1"], METADATA_TYPE, "series", 0);
  index_->SetMetadata(*manager_, ids_["sa1-1"], METADATA_TYPE + 1, "other", 0);

  Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
  request.set_orthanc_id_patient("pa");
  request.mutable_children_series()->set_retrieve_identifiers(true);
  request.mutable_children_series()->add_retrieve_metadata(METADATA_TYPE);
  Messages::Find_Request_Tag* tag = request.mutable_children_series()->add_retrieve_main_dicom_tags();
  tag->set_group(GROUP_0008);
  tag->set_element(MODALITY);
  request.mutable_children_instances()->set_retrieve_count(true);
  tag = request.mutable_children_instances()->add_retrieve_main_dicom_tags();
  tag->set_group(GROUP_0020);
  tag->set_element(INSTANCE_NUMBER);

  Messages::TransactionResponse response = Find(request);
  ASSERT_EQ(2, response.find_size());

  {
    const Messages::Find_Response& sa1 = response.find(0);
    ASSERT_EQ("sa1", sa1.public_id());

    const Messages::Find_Response_ChildrenContent& series = sa1.children_series_content();
    std::set<std::string> ids(series.identifiers().begin(), series.identifiers().end());
    ASSERT_EQ(MakeSet("sa1-1", "sa1-2"), ids);
    ASSERT_EQ(2u, series.count());

    std::vector<std::string> modalities = GetTagValues(series.main_dicom_tags(), GROUP_0008, MODALITY);
    std::sort(modalities.begin(), modalities.end());
    ASSERT_EQ(Ids({"CT", "SR"}), modalities);
    ASSERT_EQ(2, series.main_dicom_tags_size());  // Only the requested tags

    ASSERT_EQ(1, series.metadata_size());  // Only the requested metadata
    ASSERT_EQ("series", GetMetadataValue(series.metadata(), METADATA_TYPE));

    const Messages::Find_Response_ChildrenContent& instances = sa1.children_instances_content();
    ASSERT_EQ(5u, instances.count());
    ASSERT_EQ(0, instances.identifiers_size());
    ASSERT_EQ(4, instances.main_dicom_tags_size());  // "sa1-1-i4" has no instance number
  }

  {
    const Messages::Find_Response& sa2 = response.find(1);
    ASSERT_EQ("sa2", sa2.public_id());
    ASSERT_EQ(1u, sa2.children_series_content().count());
    ASSERT_EQ(0, sa2.children_series_content().metadata_size());
    ASSERT_EQ(1u, sa2.children_instances_content().count());
  }

  // From the patients, with a constraint on a child level
  request = CreateRequest(Messages::RESOURCE_PATIENT);
  AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::CONSTRAINT_WILDCARD, "CT*");
  request.mutable_children_studies()->set_retrieve_count(true);
  request.mutable_children_series()->set_retrieve_count(true);
  request.mutable_children_instances()->set_retrieve_identifiers(true);

  response = Find(request);
  ASSERT_EQ(2, response.find_size());
  ASSERT_EQ("pa", response.find(0).public_id());
  ASSERT_EQ(2u, response.find(0).children_studies_content().count());
  ASSERT_EQ(3u, response.find(0).children_series_content().count());
  ASSERT_EQ(6u, response.find(0).children_instances_content().count());
  ASSERT_EQ(6, response.find(0).children_instances_content().identifiers_size());
  ASSERT_EQ("pb", response.find(1).public_id());
  ASSERT_EQ(1u, response.find(1).children_studies_content().count());
  ASSERT_EQ(Ids({"sb1-1-i1"}), Ids(response.find(1).children_instances_content().identifiers().begin(),
                                   response.find(1).children_instances_content().identifiers().end()));

  // A series without instance
  Create("sb1-2", OrthancPluginResourceType_Series, "sb1");
  request = CreateRequest(Messages::RESOURCE_SERIES);
  request.set_orthanc_id_series("sb1-2");
  request.mutable_children_instances()->set_retrieve_count(true);
  request.mutable_children_instances()->set_retrieve_identifiers(true);
  request.set_retrieve_one_instance_metadata_and_attachments(true);
  response = Find(request);
  ASSERT_EQ(1, response.find_size());
  ASSERT_EQ(0u, response.find(0).children_instances_content().count());
  ASSERT_TRUE(response.find(0).one_instance_public_id().empty());
}


TEST_F(MongoDBFindTest, FindAncestorsAndOneInstance)
{
  index_->SetMetadata(*manager_, ids_["sa1"], METADATA_TYPE, "study", 2);
  index_->SetMetadata(*manager_, ids_["sa1-1-i1"], METADATA_TYPE, "instance", 5);
  AddAttachment(ids_["sa1-1-i1"], "uuid-i1");

  // Instances, with the content of all their ancestors
  Messages::Find_Request request = CreateRequest(Messages::RESOURCE_INSTANCE);
  AddTagConstraint(request, Messages::RESOURCE_INSTANCE, GROUP_0020, INSTANCE_NUMBER, Messages::CONSTRAINT_EQUAL, "1");
  request.set_retrieve_parent_identifier(true);
  request.mutable_parent_series()->set_retrieve_main_dicom_tags(true);
  request.mutable_parent_study()->set_retrieve_metadata(true);
  request.mutable_parent_patient()->set_retrieve_main_dicom_tags(true);
  request.set_retrieve_one_instance_metadata_and_attachments(true);  // Ignored at the instance level

  Messages::TransactionResponse response = Find(request);
  ASSERT_EQ(4, response.find_size());

  const Messages::Find_Response& i1 = response.find(0);
  ASSERT_EQ("sa1-1-i1", i1.public_id());
  ASSERT_EQ("sa1-1", i1.parent_public_id());
  ASSERT_EQ(Ids({"CT"}), GetTagValues(i1.series_content().main_dicom_tags(), GROUP_0008, MODALITY));
  ASSERT_EQ("study", GetMetadataValue(i1.study_content().metadata(), METADATA_TYPE));
  ASSERT_EQ(0, i1.study_content().main_dicom_tags_size());
  ASSERT_EQ(Ids({"DOE"}), GetTagValues(i1.patient_content().main_dicom_tags(), GROUP_0010, PATIENT_NAME));
  ASSERT_TRUE(i1.one_instance_public_id().empty());

  ASSERT_EQ("sb1-1-i1", response.find(3).public_id());
  ASSERT_EQ(Ids({"SMITH"}), GetTagValues(response.find(3).patient_content().main_dicom_tags(), GROUP_0010, PATIENT_NAME));

  // One instance of each study, with its metadata and attachments
  request = CreateRequest(Messages::RESOURCE_STUDY);
  AddTagConstraint(request, Messages::RESOURCE_SERIES, GROUP_0008, MODALITY, Messages::CONSTRAINT_EQUAL, "CT");
  request.set_retrieve_one_instance_metadata_and_attachments(true);
  request.set_retrieve_parent_identifier(true);

  response = Find(request);
  ASSERT_EQ(2, response.find_size());

  const Messages::Find_Response& sa1 = response.find(0);
  ASSERT_EQ("sa1", sa1.public_id());
  ASSERT_EQ("pa", sa1.parent_public_id());
  ASSERT_EQ("sa1-1-i1", sa1.one_instance_public_id());  // The first stored instance

  int64_t revision = -1;
  ASSERT_EQ(1, sa1.one_instance_metadata_size());
  ASSERT_EQ("instance", GetMetadataValue(sa1.one_instance_metadata(), METADATA_TYPE, &revision));
  ASSERT_EQ(5, revision);
  ASSERT_EQ(1, sa1.one_instance_attachments_size());
  ASSERT_EQ("uuid-i1", sa1.one_instance_attachments(0).uuid());
  ASSERT_TRUE(sa1.one_instance_attachments(0).custom_data().empty());

  ASSERT_EQ("sb1", response.find(1).public_id());
  ASSERT_EQ("pb", response.find(1).parent_public_id());
  ASSERT_EQ("sb1-1-i1", response.find(1).one_instance_public_id());
  ASSERT_EQ(0, response.find(1).one_instance_metadata_size());
}


// The pipeline shapes whose "$lookup"s use a level array as "localField" (cf. "GetLevelKey()")
TEST_F(MongoDBFindTest, FindLevelArraysAfterEachStart)
{
  index_->AddLabel(*manager_, ids_["sa1-1"], "l");
  index_->AddLabel(*manager_, ids_["sb1-1"], "l");

  for (int start = 0; start < 5; start++)
  {
    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_SERIES);

    switch (start)
    {
      case 0:  // No driver
        break;

      case 1:  // Driver on the query level
        AddTagConstraint(request, Messages::RESOURCE_SERIES, GROUP_0008, MODALITY, Messages::CONSTRAINT_EQUAL, "CT");
        break;

      case 2:  // Driver on a lower level, then "$group"
        AddTagConstraint(request, Messages::RESOURCE_INSTANCE, GROUP_0020, INSTANCE_NUMBER, Messages::CONSTRAINT_EQUAL, "1");
        break;

      case 3:  // Driver on an ancestor level
        AddTagConstraint(request, Messages::RESOURCE_PATIENT, GROUP_0010, PATIENT_NAME, Messages::CONSTRAINT_EQUAL, "DOE");
        break;

      case 4:  // Labels
        request.add_labels("l");
        request.set_labels_constraint(Messages::LABELS_CONSTRAINT_ANY);
        break;

      default:
        throw std::runtime_error("unexpected");
    }

    request.set_retrieve_parent_identifier(true);
    request.mutable_parent_patient()->set_retrieve_main_dicom_tags(true);
    request.mutable_children_instances()->set_retrieve_count(true);
    request.mutable_children_instances()->set_retrieve_identifiers(true);
    request.set_retrieve_one_instance_metadata_and_attachments(true);
    AddTagOrdering(request, Messages::RESOURCE_PATIENT, GROUP_0010, PATIENT_NAME, Messages::ORDERING_DIRECTION_ASC);

    Messages::TransactionResponse response = Find(request);
    ASSERT_LE(2, response.find_size()) << start;

    for (int i = 0; i < response.find_size(); i++)
    {
      const Messages::Find_Response& series = response.find(i);
      ASSERT_FALSE(series.parent_public_id().empty()) << start;
      ASSERT_EQ(series.public_id().substr(0, series.parent_public_id().size()), series.parent_public_id()) << start;
      ASSERT_EQ(1, series.patient_content().main_dicom_tags_size()) << start;
      ASSERT_LE(1u, series.children_instances_content().count()) << start;
      ASSERT_EQ(series.public_id() + "-i1", series.one_instance_public_id()) << start;
    }

    // The patient "DOE" comes first
    ASSERT_EQ("sa1-1", response.find(0).public_id()) << start;
  }
}


/**
 * The pages of a list ordered by a tag of the query level use the
 * bound of "FindOrderingBound()": each page must be the same slice of
 * the whole list (which is read without limits, hence without bound),
 * including with ties at the end of a page, resources without the tag,
 * filters, and a resource with two documents for the tag.
 **/
TEST_F(MongoDBFindTest, FindOrderingPages)
{
  // More studies, with ties on the date, and two without date
  const char* dates[] = { "20200101", "20200101", "20180505", NULL, "20200101", "20221212", NULL, "20180505" };
  for (size_t i = 0; i < sizeof(dates) / sizeof(const char*); i++)
  {
    const std::string study = "sc" + std::to_string(i);
    Create(study, OrthancPluginResourceType_Study, "pb");
    if (dates[i] != NULL)
    {
      SetTag(study, GROUP_0008, STUDY_DATE, dates[i]);
    }
    SetTag(study, GROUP_0008, STUDY_DESCRIPTION, i % 3 == 0 ? "MR" : "CT");
  }

  index_->AddLabel(*manager_, ids_["sc1"], "hidden");
  index_->AddLabel(*manager_, ids_["sa1"], "hidden");

  for (int variant = 0; variant < 5; variant++)
  {
    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);

    switch (variant)
    {
      case 0:
        AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::ORDERING_DIRECTION_DESC);
        break;

      case 1:
        AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::ORDERING_DIRECTION_ASC);
        AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::ORDERING_DIRECTION_DESC);
        break;

      case 2:  // Filters applied while reading the index
        AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::ORDERING_DIRECTION_DESC);
        AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::CONSTRAINT_EQUAL, "CT", true, false);
        request.add_labels("hidden");
        request.set_labels_constraint(Messages::LABELS_CONSTRAINT_NONE);
        break;

      case 3:  // The patient tags are copied at the study level
        AddTagOrdering(request, Messages::RESOURCE_PATIENT, GROUP_0010, PATIENT_NAME, Messages::ORDERING_DIRECTION_ASC);
        break;

      case 4:
      {
        // A second document for the tag of one study, which Orthanc never writes
        mongocxx::client client{mongocxx::uri{database_->GetUri()}};
        GetCollection(client, "MainDicomTags").insert_one(make_document(
          kvp("id", ids_["sc5"]), kvp("tagGroup", static_cast<int32_t>(GROUP_0008)),
          kvp("tagElement", static_cast<int32_t>(STUDY_DATE)), kvp("value", "20221212")));
        AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::ORDERING_DIRECTION_DESC);
        break;
      }

      default:
        throw std::runtime_error("unexpected");
    }

    const Ids all = FindIds(request);
    ASSERT_EQ(variant == 2 ? 4u : 11u, all.size()) << variant;  // Exactly "CT", without "sc1"

    for (size_t since = 0; since <= all.size(); since++)
    {
      for (size_t count = 1; since + count <= all.size() + 1; count++)
      {
        request.mutable_limits()->set_since(since);
        request.mutable_limits()->set_count(count);

        const Ids expected(all.begin() + since, all.begin() + std::min(all.size(), since + count));
        ASSERT_EQ(expected, FindIds(request)) << variant << " since=" << since << " count=" << count;
      }
    }
  }
}


/**
 * A mandatory tag that matches more than "PROBE_LIMIT" tags is not
 * selective: the pages then come from an ordering scan, filtered by
 * this tag (and by the others). Each page must be the same slice of
 * the whole list, including when the filters reject so many resources
 * that the scan reaches its limit and the request runs without it.
 **/
TEST_F(MongoDBFindTest, FindOrderingScanWithFilters)
{
  static const uint16_t ACCESSION_NUMBER = 0x0050;  // (0008,0050), study

  const size_t COUNT = 1200;
  for (size_t i = 0; i < COUNT; i++)
  {
    const std::string study = "sd" + std::to_string(i);
    Create(study, OrthancPluginResourceType_Study, "pa");
    SetTag(study, GROUP_0008, STUDY_DESCRIPTION, "CT");
    SetTag(study, GROUP_0008, STUDY_DATE, "2019" + std::to_string(1000 + (i * 7919) % 1200));  // Shuffled, with ties
    SetTag(study, GROUP_0008, ACCESSION_NUMBER, "A" + std::to_string(i));
  }

  for (int variant = 0; variant < 6; variant++)
  {
    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_STUDY);
    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DESCRIPTION, Messages::CONSTRAINT_EQUAL, "CT");

    size_t expected = COUNT;  // The studies of the fixture have other descriptions

    if (variant == 1 || variant == 3)
    {
      // One study in 10: the scan finds the pages
      AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, ACCESSION_NUMBER, Messages::CONSTRAINT_WILDCARD, "*7");
      expected = COUNT / 10;
    }
    else if (variant == 4 || variant == 5)
    {
      // One study in 50: the scan reaches its limit
      AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, ACCESSION_NUMBER, Messages::CONSTRAINT_WILDCARD, "*12", false);
      expected = 24;  // "A12", "A112", ..., "A1112", and "A12x"
    }

    if (variant == 0 || variant == 1 || variant == 4)
    {
      AddTagOrdering(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::ORDERING_DIRECTION_DESC);
    }

    const Ids all = FindIds(request);
    if (variant != 4 && variant != 5)
    {
      ASSERT_EQ(expected, all.size()) << variant;
    }

    for (size_t since = 0; since < all.size() + 2; since = (since < 3 ? since + 1 : since * 4))
    {
      for (size_t count = 1; count <= 100; count = count * 10)
      {
        request.mutable_limits()->set_since(since);
        request.mutable_limits()->set_count(count);

        const Ids page(all.begin() + std::min(all.size(), since), all.begin() + std::min(all.size(), since + count));
        ASSERT_EQ(page, FindIds(request)) << variant << " since=" << since << " count=" << count;
        ASSERT_EQ(page.size(), Count(request)) << variant << " since=" << since << " count=" << count;
      }
    }
  }
}


/**
 * Two mandatory tags that both match more than "PROBE_LIMIT" tags are
 * counted again with a higher limit, so that the more selective one
 * drives: here, the study date range (1,100 studies) rather than the
 * modality (1,300 series), including when the modality is matched
 * case-insensitively, which does not bound the index scan. Both give
 * the same answers, so the test reads the pipeline of the Find in the
 * profiler of the test database.
 **/
TEST_F(MongoDBFindTest, FindDriverAboveProbeLimit)
{
  const size_t DATED = 1100;
  const size_t OTHERS = 200;

  for (size_t i = 0; i < DATED; i++)
  {
    const std::string study = "sd" + std::to_string(i);
    const std::string series = study + "-1";
    Create(study, OrthancPluginResourceType_Study, "pa");
    Create(series, OrthancPluginResourceType_Series, study);
    SetTag(study, GROUP_0008, STUDY_DATE, "201906" + std::to_string(10 + i % 20));
    SetTag(series, GROUP_0008, MODALITY, "MR");
  }

  Create("sx", OrthancPluginResourceType_Study, "pb");  // No date
  for (size_t i = 0; i < OTHERS; i++)
  {
    const std::string series = "sx-" + std::to_string(i);
    Create(series, OrthancPluginResourceType_Series, "sx");
    SetTag(series, GROUP_0008, MODALITY, "MR");
  }

  mongocxx::client client{mongocxx::uri{database_->GetUri()}};
  mongocxx::database database = client[database_->GetName()];

  // A case-insensitive equality is a regular expression, which does not bound the index scan
  for (bool caseSensitive : { true, false })
  {
    Messages::Find_Request request = CreateRequest(Messages::RESOURCE_SERIES);
    AddTagConstraint(request, Messages::RESOURCE_SERIES, GROUP_0008, MODALITY, Messages::CONSTRAINT_EQUAL, "MR", caseSensitive);
    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::CONSTRAINT_GREATER_OR_EQUAL, "20190601");
    AddTagConstraint(request, Messages::RESOURCE_STUDY, GROUP_0008, STUDY_DATE, Messages::CONSTRAINT_SMALLER_OR_EQUAL, "20190630");

    database["system.profile"].drop();
    database.run_command(make_document(kvp("profile", 2)));

    const size_t found = FindIds(request).size();

    database.run_command(make_document(kvp("profile", 0)));
    ASSERT_EQ(DATED, found) << caseSensitive;  // "sa2-1" of the fixture is an MR series of 2021

    // The first "$match" of the Find, the only aggregation that sorts (by public ID, without ordering)
    int32_t driverElement = -1;
    for (const bsoncxx::document::view& entry : database["system.profile"].find(
           make_document(kvp("command.aggregate", make_document(kvp("$exists", true))))))
    {
      const bsoncxx::array::view pipeline = entry["command"]["pipeline"].get_array().value;

      bool sorts = false;
      for (const bsoncxx::array::element& stage : pipeline)
      {
        sorts = (sorts || stage.get_document().value.find("$sort") != stage.get_document().value.end());
      }

      if (sorts)
      {
        const bsoncxx::document::view match = pipeline[0].get_document().value["$match"].get_document().value;
        driverElement = MongoDBToolbox::GetInteger(match, "tagElement");
      }
    }

    ASSERT_EQ(STUDY_DATE, driverElement) << caseSensitive;
  }
}
