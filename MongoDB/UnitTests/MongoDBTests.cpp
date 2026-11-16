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

