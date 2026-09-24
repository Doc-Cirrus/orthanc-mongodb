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


#pragma once

#include "MongoDBCollection.h"
#include "MongoDBParameters.h"

#include "../Common/IDatabase.h"
#include "../Common/IDatabaseFactory.h"

#include <memory>


namespace OrthancDatabases
{
  class DatabaseManager;

  /**
   * Counterpart of "PostgreSQLDatabase": one connection to the
   * MongoDB server, as seen by one "DatabaseManager".
   *
   * All the connections created by the same factory share a single
   * "mongocxx::pool". Each connection keeps one client of this pool
   * for its whole lifetime, so that a client is never used by two
   * threads at once.
   **/
  class MongoDBDatabase : public IDatabase
  {
  public:
    class Pool;

  private:
    class Factory;

    std::shared_ptr<Pool>    pool_;    // Must outlive "client_"
    mongocxx::pool::entry      client_;
    mongocxx::client_session*  session_;  // Session of the active transaction, if any

  public:
    explicit MongoDBDatabase(const std::shared_ptr<Pool>& pool);

    virtual ~MongoDBDatabase();

    mongocxx::client& GetClient()
    {
      return *client_;
    }

    /**
     * Another client of the same pool, for a helper thread that runs
     * next to this connection (a client is not thread-safe).
     **/
    mongocxx::pool::entry AcquireClient();

    const std::string& GetDatabaseName() const;

    mongocxx::database GetDatabase();

    // The returned collection takes part in the active transaction, if any
    MongoDBCollection GetCollection(const std::string& name);

    // Whether multi-document transactions are used on this server
    bool HasTransactions() const;

    bool IsWritablePrimary();

    int64_t GetNextSequence(const std::string& sequence);

    /**
     * The session of the active transaction, or NULL. For the objects
     * that "GetCollection()" does not cover, such as the GridFS buckets.
     **/
    mongocxx::client_session* GetSession() const
    {
      return session_;
    }

    // Only for "MongoDBTransaction"
    void SetSession(mongocxx::client_session* session)
    {
      session_ = session;
    }

    virtual ITransaction* CreateTransaction(TransactionType type) ORTHANC_OVERRIDE;

    static MongoDBDatabase& GetDatabase(DatabaseManager& manager);

    /**
     * The pool is shared by all the factories created from it, so that
     * all the connections of the plugin use a single "mongocxx::pool".
     **/
    static std::shared_ptr<Pool> CreatePool(const MongoDBParameters& parameters);

    static IDatabaseFactory* CreateDatabaseFactory(const std::shared_ptr<Pool>& pool);

    static IDatabaseFactory* CreateDatabaseFactory(const MongoDBParameters& parameters);

    static MongoDBDatabase* CreateDatabaseConnection(const MongoDBParameters& parameters);

    /**
     * The plugin needs MongoDB 7.0 or later, i.e. a wire version of at
     * least 21. Throws "IncompatibleDatabaseVersion" otherwise.
     **/
    static void CheckServerVersion(const bsoncxx::document::view& hello);

    /**
     * Keeps the shared library of the calling plugin in memory until
     * Orthanc exits, even after Orthanc unloads it. To be called once
     * by "OrthancPluginInitialize()".
     *
     * Both plugins link the MongoDB C driver statically, but they share
     * the system library "libsasl2" (Kerberos authentication). It keeps
     * the mutex functions of the first plugin that initializes the
     * driver, and calls them again when the other plugin finalizes the
     * driver. Orthanc has unloaded the first plugin by then, which made
     * Orthanc crash at shutdown whenever both plugins were loaded.
     **/
    static void KeepPluginLoaded();

    /**
     * Converts an exception of the MongoDB driver into the Orthanc
     * error that lets the core react properly: "DatabaseCannotSerialize"
     * makes Orthanc retry the whole transaction, and
     * "DatabaseUnavailable" makes the "DatabaseManager" reconnect.
     **/
    static Orthanc::ErrorCode ConvertErrorCode(const mongocxx::exception& e);

    [[noreturn]] static void ThrowException(const mongocxx::exception& e);
  };
}
