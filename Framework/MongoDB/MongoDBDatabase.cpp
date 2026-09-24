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


#include "MongoDBDatabase.h"

#include "MongoDBTransaction.h"
#include "../Common/DatabaseManager.h"
#include "../Common/RetryDatabaseFactory.h"

#include <Logging.h>
#include <OrthancException.h>

#include <boost/thread/mutex.hpp>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#  include <dlfcn.h>
#endif


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  /**
   * The "mongocxx::pool" shared by all the connections of one
   * factory, together with what was learnt about the server.
   **/
  class MongoDBDatabase::Pool : public boost::noncopyable
  {
  private:
    MongoDBParameters  parameters_;
    std::string        databaseName_;
    mongocxx::pool     pool_;
    boost::mutex       mutex_;
    bool               detected_;
    bool               transactions_;

  public:
    explicit Pool(const MongoDBParameters& parameters) :
      parameters_(parameters),
      databaseName_(parameters.GetDatabaseName()),
      pool_(mongocxx::uri{parameters.GetConnectionUri()}),
      detected_(false),
      transactions_(false)
    {
    }

    const MongoDBParameters& GetParameters() const
    {
      return parameters_;
    }

    const std::string& GetDatabaseName() const
    {
      return databaseName_;
    }

    mongocxx::pool::entry Acquire()
    {
      return pool_.acquire();
    }

    /**
     * Sends "hello" to the server, which fails if the server cannot
     * be reached (the pool itself connects lazily). The first
     * successful call decides whether transactions are used.
     **/
    void Connect(mongocxx::client& client)
    {
      bsoncxx::document::value hello = client["admin"].run_command(make_document(kvp("hello", 1)));

      boost::mutex::scoped_lock lock(mutex_);

      if (detected_)
      {
        return;
      }

      const bsoncxx::document::view view = hello.view();
      CheckServerVersion(view);

      const bool isReplicaSet = (view.find("setName") != view.end());
      const bool isShardedCluster = (view.find("msg") != view.end() &&
                                     view["msg"].type() == bsoncxx::type::k_string &&
                                     view["msg"].get_string().value == "isdbgrid");
      const bool supported = (isReplicaSet || isShardedCluster);

      switch (parameters_.GetTransactionsMode())
      {
        case MongoDBTransactionsMode_Auto:
          transactions_ = supported;
          break;

        case MongoDBTransactionsMode_Enabled:
          if (!supported)
          {
            throw Orthanc::OrthancException(
              Orthanc::ErrorCode_BadFileFormat,
              "\"EnableTransactions\" is true, but the MongoDB server is standalone: "
              "multi-document transactions need a replica set or a sharded cluster");
          }
          transactions_ = true;
          break;

        case MongoDBTransactionsMode_Disabled:
          transactions_ = false;
          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }

      if (transactions_)
      {
        LOG(WARNING) << "MongoDB: the server is a " << (isReplicaSet ? "replica set" : "sharded cluster")
                     << ", multi-document transactions are enabled";
      }
      else if (supported)
      {
        LOG(WARNING) << "MongoDB: multi-document transactions are disabled by the configuration";
      }
      else
      {
        LOG(WARNING) << "MongoDB: the server is standalone, multi-document transactions are not available";
      }

      detected_ = true;
    }

    bool HasTransactions()
    {
      boost::mutex::scoped_lock lock(mutex_);

      if (!detected_)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
      }

      return transactions_;
    }
  };


  class MongoDBDatabase::Factory : public RetryDatabaseFactory
  {
  private:
    std::shared_ptr<Pool>  pool_;

  protected:
    virtual IDatabase* TryOpen() ORTHANC_OVERRIDE
    {
      return new MongoDBDatabase(pool_);
    }

  public:
    explicit Factory(const std::shared_ptr<Pool>& pool) :
      RetryDatabaseFactory(pool->GetParameters().GetMaxConnectionRetries(),
                           pool->GetParameters().GetConnectionRetryInterval()),
      pool_(pool)
    {
    }
  };


  MongoDBDatabase::MongoDBDatabase(const std::shared_ptr<Pool>& pool) :
    pool_(pool),
    client_(pool->Acquire()),
    session_(NULL)
  {
    try
    {
      pool_->Connect(*client_);
    }
    catch (mongocxx::exception& e)
    {
      ThrowException(e);
    }
  }


  MongoDBDatabase::~MongoDBDatabase()
  {
    // The client is given back to the pool before the pool is released
    client_ = nullptr;
  }


  void MongoDBDatabase::KeepPluginLoaded()
  {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
    // The shared library that contains this function, i.e. the plugin that calls it
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&MongoDBDatabase::KeepPluginLoaded), &info) == 0 ||
        info.dli_fname == NULL)
    {
      LOG(WARNING) << "MongoDB: cannot locate the shared library of the plugin, Orthanc may crash when it stops";
      return;
    }

    // "RTLD_NOLOAD" only takes one more reference on the library, which is already loaded, and "RTLD_NODELETE"
    // keeps it mapped after "dlclose()". The handle is never closed.
    if (dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE) == NULL)
    {
      LOG(WARNING) << "MongoDB: cannot keep " << info.dli_fname << " loaded, Orthanc may crash when it stops";
    }
#endif
  }


  void MongoDBDatabase::CheckServerVersion(const bsoncxx::document::view& hello)
  {
    // https://github.com/mongodb/specifications/blob/master/source/wireversion-featurelist/wireversion-featurelist.md
    static const int32_t MONGODB_7_0_WIRE_VERSION = 21;

    const bsoncxx::document::element element = hello["maxWireVersion"];
    int32_t version = 0;

    if (element &&
        element.type() == bsoncxx::type::k_int32)
    {
      version = element.get_int32().value;
    }
    else if (element &&
             element.type() == bsoncxx::type::k_int64)
    {
      version = static_cast<int32_t>(element.get_int64().value);
    }

    if (version < MONGODB_7_0_WIRE_VERSION)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_IncompatibleDatabaseVersion,
                                      "The MongoDB server is too old (wire version " + std::to_string(version) +
                                      "): this plugin needs MongoDB 7.0 or later");
    }
  }


  mongocxx::pool::entry MongoDBDatabase::AcquireClient()
  {
    return pool_->Acquire();
  }


  const std::string& MongoDBDatabase::GetDatabaseName() const
  {
    return pool_->GetDatabaseName();
  }


  mongocxx::database MongoDBDatabase::GetDatabase()
  {
    return (*client_)[pool_->GetDatabaseName()];
  }


  MongoDBCollection MongoDBDatabase::GetCollection(const std::string& name)
  {
    return MongoDBCollection(GetDatabase()[name], session_);
  }


  bool MongoDBDatabase::HasTransactions() const
  {
    return pool_->HasTransactions();
  }


  bool MongoDBDatabase::IsWritablePrimary()
  {
    try
    {
      bsoncxx::document::value hello = GetDatabase().run_command(make_document(kvp("hello", 1)));
      bsoncxx::document::element element = hello.view()["isWritablePrimary"];
      return (element && element.type() == bsoncxx::type::k_bool && element.get_bool().value);
    }
    catch (mongocxx::exception& e)
    {
      ThrowException(e);
    }
  }


  int64_t MongoDBDatabase::GetNextSequence(const std::string& sequence)
  {
    /**
     * Like the sequences of PostgreSQL, the counters are never rolled
     * back: they are incremented outside of the active transaction.
     * Otherwise, all the concurrent transactions that create a
     * resource would conflict on the same counter document.
     **/
    mongocxx::options::find_one_and_update options;
    options.upsert(true);
    options.return_document(mongocxx::options::return_document::k_after);

    MongoDBCollection sequences(GetDatabase()["Sequences"], NULL);

    std::optional<bsoncxx::document::value> counter = sequences.FindOneAndUpdate(
      make_document(kvp("name", sequence)),
      make_document(kvp("$inc", make_document(kvp("i", int64_t(1))))),
      options);

    if (!counter)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
    }

    bsoncxx::document::element value = counter->view()["i"];

    switch (value.type())
    {
      case bsoncxx::type::k_int64:
        return value.get_int64().value;

      case bsoncxx::type::k_int32:  // Counter created by some external tool
        return value.get_int32().value;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database,
                                        "Corrupted MongoDB sequence: " + sequence);
    }
  }


  ITransaction* MongoDBDatabase::CreateTransaction(TransactionType type)
  {
    return new MongoDBTransaction(*this, type);
  }


  MongoDBDatabase& MongoDBDatabase::GetDatabase(DatabaseManager& manager)
  {
    return dynamic_cast<MongoDBDatabase&>(manager.GetDatabase());
  }


  std::shared_ptr<MongoDBDatabase::Pool> MongoDBDatabase::CreatePool(const MongoDBParameters& parameters)
  {
    try
    {
      return std::make_shared<Pool>(parameters);
    }
    catch (mongocxx::exception& e)
    {
      // Typically, an invalid connection URI
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadFileFormat,
                                      "Invalid MongoDB connection URI: " + std::string(e.what()));
    }
  }


  IDatabaseFactory* MongoDBDatabase::CreateDatabaseFactory(const std::shared_ptr<Pool>& pool)
  {
    if (pool.get() == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }

    return new Factory(pool);
  }


  IDatabaseFactory* MongoDBDatabase::CreateDatabaseFactory(const MongoDBParameters& parameters)
  {
    return new Factory(CreatePool(parameters));
  }


  MongoDBDatabase* MongoDBDatabase::CreateDatabaseConnection(const MongoDBParameters& parameters)
  {
    Factory factory(CreatePool(parameters));
    return dynamic_cast<MongoDBDatabase*>(factory.Open());
  }


  static bool IsDuplicateKey(const mongocxx::operation_exception& e)
  {
    if (e.code().value() == 11000 /* DuplicateKey */)
    {
      return true;
    }

    // Bulk writes report their errors in the raw server reply
    if (e.raw_server_error())
    {
      const bsoncxx::document::view reply = e.raw_server_error()->view();
      bsoncxx::document::element writeErrors = reply["writeErrors"];

      if (writeErrors && writeErrors.type() == bsoncxx::type::k_array)
      {
        for (const bsoncxx::array::element& error : writeErrors.get_array().value)
        {
          if (error.type() == bsoncxx::type::k_document &&
              error["code"] &&
              error["code"].type() == bsoncxx::type::k_int32 &&
              error["code"].get_int32().value == 11000)
          {
            return true;
          }
        }
      }
    }

    return false;
  }


  Orthanc::ErrorCode MongoDBDatabase::ConvertErrorCode(const mongocxx::exception& e)
  {
    const mongocxx::operation_exception* operation = dynamic_cast<const mongocxx::operation_exception*>(&e);

    if (operation != NULL)
    {
      if (operation->has_error_label("TransientTransactionError") ||
          operation->has_error_label("UnknownTransactionCommitResult") ||
          IsDuplicateKey(*operation))
      {
        // A retry of the whole Orthanc transaction can succeed
        return Orthanc::ErrorCode_DatabaseCannotSerialize;
      }
    }

    switch (e.code().value())
    {
      case 112:    // WriteConflict
      case 251:    // NoSuchTransaction
        return Orthanc::ErrorCode_DatabaseCannotSerialize;

      case MONGOC_ERROR_SERVER_SELECTION_FAILURE:
      case 6:      // HostUnreachable
      case 7:      // HostNotFound
      case 89:     // NetworkTimeout
      case 91:     // ShutdownInProgress
      case 189:    // PrimarySteppedDown
      case 10107:  // NotWritablePrimary
      case 11600:  // InterruptedAtShutdown
      case 11602:  // InterruptedDueToReplStateChange
      case 13435:  // NotPrimaryNoSecondaryOk
        return Orthanc::ErrorCode_DatabaseUnavailable;

      default:
        return Orthanc::ErrorCode_Database;
    }
  }


  void MongoDBDatabase::ThrowException(const mongocxx::exception& e)
  {
    const Orthanc::ErrorCode code = ConvertErrorCode(e);

    if (code == Orthanc::ErrorCode_DatabaseCannotSerialize)
    {
      // Expected under concurrency, Orthanc retries the transaction
      throw Orthanc::OrthancException(code, e.what(), false /* don't log */);
    }
    else
    {
      throw Orthanc::OrthancException(code, "MongoDB error: " + std::string(e.what()));
    }
  }
}
