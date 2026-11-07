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

#include "MongoDBDatabase.h"

#include "../Common/ITransaction.h"

#include <memory>


namespace OrthancDatabases
{
  /**
   * Counterpart of "PostgreSQLTransaction". The strategy is chosen
   * once per server (cf. "MongoDBDatabase::HasTransactions()"):
   *
   * - On a replica set or a sharded cluster, each Orthanc transaction
   *   is a multi-document transaction of a "client_session". Every
   *   "MongoDBCollection" created from the database while this object
   *   is alive takes part in it.
   *
   * - On a standalone server, there are no multi-document
   *   transactions: each operation is applied immediately, "Commit()"
   *   does nothing, and "Rollback()" cannot undo anything. The
   *   operations must then be safe by themselves (upserts, unique
   *   indexes, ordering of the writes).
   **/
  class MongoDBTransaction : public ITransaction
  {
  private:
    MongoDBDatabase&                           database_;
    bool                                       isImplicit_;
    std::unique_ptr<mongocxx::client_session>  session_;

    void Close();

  public:
    MongoDBTransaction(MongoDBDatabase& database,
                       TransactionType type);

    virtual ~MongoDBTransaction();

    // Whether this transaction is backed by a server-side transaction
    bool HasSession() const
    {
      return session_.get() != NULL;
    }

    virtual bool IsImplicit() const ORTHANC_OVERRIDE
    {
      return isImplicit_;
    }

    virtual void Rollback() ORTHANC_OVERRIDE;

    virtual void Commit() ORTHANC_OVERRIDE;
  };
}
