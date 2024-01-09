// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IDatabaseFactory.h"
#include "ITransaction.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Enumerations.h>

#include <memory>


namespace OrthancDatabases
{
  /**
   * WARNING: In PostgreSQL releases <= 3.3 and in MySQL releases <=
   * 3.0, this class was protected by a mutex. It is now assumed that
   * locking must be implemented at a higher level.
   *
   * This class maintains a list of precompiled statements. At any
   * time, this class handles 0 or 1 active transaction.
   *
   * "DatabaseManager" takes a "IDatabaseFactory" as input, in order
   * to be able to automatically re-open the database connection if
   * the latter gets lost.
   **/
  class DatabaseManager : public boost::noncopyable
  {
  private:
    std::unique_ptr<IDatabaseFactory>  factory_;
    std::unique_ptr<IDatabase>     database_;
    std::unique_ptr<ITransaction>  transaction_;

    void CloseIfUnavailable(Orthanc::ErrorCode e);

    ITransaction& GetTransaction();

  public:
    explicit DatabaseManager(IDatabaseFactory* factory);  // Takes ownership

    ~DatabaseManager()
    {
      Close();
    }

    IDatabase& GetDatabase();

    void Close();

    void StartTransaction(TransactionType type);

    void CommitTransaction();

    void RollbackTransaction();


    // This class is only used in the "StorageBackend" and in
    // "IDatabaseBackend::ConfigureDatabase()"
    class Transaction : public boost::noncopyable
    {
    private:
      DatabaseManager&  manager_;
      IDatabase&        database_;
      bool              active_;

    public:
      explicit Transaction(DatabaseManager& manager,
                           TransactionType type);

      ~Transaction();

      void Commit();

      void Rollback();

      /**
       * WARNING: Don't call "GetDatabaseTransaction().Commit()" and
       * "GetDatabaseTransaction().Rollback()", but use the "Commit()"
       * and "Rollback()" methods above.
       **/
      ITransaction& GetDatabaseTransaction()
      {
        return manager_.GetTransaction();
      }
    };
  };
}
