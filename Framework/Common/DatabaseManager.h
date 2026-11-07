/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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
// (without the cache of precompiled SQL statements)

#pragma once

#include "IDatabaseFactory.h"
#include "ITransaction.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Enumerations.h>

#include <memory>
#include <boost/date_time/posix_time/posix_time.hpp>


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
    boost::posix_time::ptime       creationTime_;
    boost::posix_time::ptime       lastUseTime_;

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

    uint64_t GetElapsedSecondsSinceCreation() const;

    uint64_t GetElapsedSecondsSinceLastUse() const;

    /**
     * MongoDB: Returns the transaction that is currently active on
     * this connection, or NULL if none. The database operations use
     * it to attach themselves to the underlying server session (if
     * any). Contrarily to SQL engines, no implicit transaction is
     * created if there is no active transaction.
     **/
    ITransaction* LookupTransaction() const
    {
      return transaction_.get();
    }


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
