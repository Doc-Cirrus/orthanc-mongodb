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


#include "MongoDBTransaction.h"

#include <Logging.h>
#include <OrthancException.h>


namespace OrthancDatabases
{
  MongoDBTransaction::MongoDBTransaction(MongoDBDatabase& database,
                                         TransactionType type) :
    database_(database),
    isImplicit_(type == TransactionType_Implicit)
  {
    if (database_.HasTransactions())
    {
      try
      {
        session_.reset(new mongocxx::client_session(database_.GetClient().start_session()));

        // The default read and write concerns of the connection URI apply
        session_->start_transaction();
      }
      catch (mongocxx::exception& e)
      {
        session_.reset();
        MongoDBDatabase::ThrowException(e);
      }

      database_.SetSession(session_.get());
    }
  }


  void MongoDBTransaction::Close()
  {
    database_.SetSession(NULL);
    session_.reset();
  }


  MongoDBTransaction::~MongoDBTransaction()
  {
    if (session_.get() != NULL)
    {
      // Neither committed nor rolled back: the server-side
      // transaction is aborted when the session is destroyed
      Close();
    }
  }


  void MongoDBTransaction::Rollback()
  {
    if (session_.get() == NULL)
    {
      LOG(INFO) << "MongoDB: no multi-document transaction on this server, nothing can be rolled back";
      return;
    }

    try
    {
      session_->abort_transaction();
    }
    catch (mongocxx::exception& e)
    {
      // The transaction is aborted anyway once the session is gone
      LOG(WARNING) << "MongoDB: error while aborting a transaction: " << e.what();
    }

    Close();
  }


  void MongoDBTransaction::Commit()
  {
    if (session_.get() == NULL)
    {
      return;
    }

    try
    {
      // The outcome of a commit can be unknown, e.g. after a network
      // error. Committing again is safe, and is what the MongoDB
      // specification recommends.
      static const unsigned int MAX_COMMIT_ATTEMPTS = 3;

      for (unsigned int attempt = 1; ; attempt++)
      {
        try
        {
          session_->commit_transaction();
          break;
        }
        catch (mongocxx::operation_exception& e)
        {
          if (attempt < MAX_COMMIT_ATTEMPTS &&
              e.has_error_label("UnknownTransactionCommitResult"))
          {
            LOG(WARNING) << "MongoDB: unknown result of a commit, retrying: " << e.what();
          }
          else
          {
            throw;
          }
        }
      }
    }
    catch (mongocxx::exception& e)
    {
      Close();
      MongoDBDatabase::ThrowException(e);
    }

    Close();
  }
}
