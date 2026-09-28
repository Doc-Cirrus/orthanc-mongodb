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

#include "DatabaseManager.h"
#include "ITransaction.h"

#include <Compatibility.h> // For std::unique_ptr<>
#include <Logging.h>
#include <OrthancException.h>

#include <boost/thread.hpp>

namespace OrthancDatabases
{
  void DatabaseManager::Close()
  {
    LOG(TRACE) << "Closing the connection to the database";

    // Rollback active transaction, if any
    transaction_.reset(NULL);

    // Close the database
    database_.reset(NULL);

    LOG(TRACE) << "Connection to the database is closed";
  }

  void DatabaseManager::CloseIfUnavailable(Orthanc::ErrorCode e)
  {
    if (e != Orthanc::ErrorCode_Success
#if ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 9, 2)
        && e != Orthanc::ErrorCode_DatabaseCannotSerialize
#endif
    )
    {
      transaction_.reset(NULL);
    }

    if (e == Orthanc::ErrorCode_DatabaseUnavailable)
    {
      LOG(ERROR) << "The database is not available, closing the connection";
      Close();
    }
  }

  ITransaction &DatabaseManager::GetTransaction()
  {
    if (transaction_.get() == NULL)
    {
      LOG(TRACE) << "Automatically creating an implicit database transaction";

      try
      {
        transaction_.reset(GetDatabase().CreateTransaction(TransactionType_Implicit));
      }
      catch (Orthanc::OrthancException &e)
      {
        CloseIfUnavailable(e.GetErrorCode());
        throw;
      }
    }

    assert(transaction_.get() != NULL);
    return *transaction_;
  }

  DatabaseManager::DatabaseManager(IDatabaseFactory *factory) :
    factory_(factory),
    creationTime_(boost::posix_time::second_clock::universal_time()),
    lastUseTime_(boost::posix_time::second_clock::universal_time())
  {
    if (factory == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }
  }

  uint64_t DatabaseManager::GetElapsedSecondsSinceCreation() const
  {
    boost::posix_time::ptime now = boost::posix_time::second_clock::universal_time();
    boost::posix_time::time_duration diff = now - creationTime_;
    return static_cast<uint64_t>(diff.total_seconds());
  }

  uint64_t DatabaseManager::GetElapsedSecondsSinceLastUse() const
  {
    boost::posix_time::ptime now = boost::posix_time::second_clock::universal_time();
    boost::posix_time::time_duration diff = now - lastUseTime_;
    return static_cast<uint64_t>(diff.total_seconds());
  }

  IDatabase &DatabaseManager::GetDatabase()
  {
    assert(factory_.get() != NULL);

    if (database_.get() == NULL)
    {
      database_.reset(factory_->Open());

      if (database_.get() == NULL)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_InternalError);
      }
    }

    return *database_;
  }

  void DatabaseManager::StartTransaction(TransactionType type)
  {
    try
    {
      if (transaction_.get() != NULL)
      {
        LOG(ERROR) << "Cannot start another transaction while there is an uncommitted transaction";
        throw Orthanc::OrthancException(Orthanc::ErrorCode_Database);
      }

      transaction_.reset(GetDatabase().CreateTransaction(type));
      lastUseTime_ = boost::posix_time::second_clock::universal_time();
    }
    catch (Orthanc::OrthancException &e)
    {
      CloseIfUnavailable(e.GetErrorCode());
      throw;
    }
  }

  void DatabaseManager::CommitTransaction()
  {
    if (transaction_.get() == NULL)
    {
      LOG(ERROR) << "Cannot commit a non-existing transaction";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      try
      {
        transaction_->Commit();
        transaction_.reset(NULL);
      }
      catch (Orthanc::OrthancException &e)
      {
        CloseIfUnavailable(e.GetErrorCode());
        throw;
      }
    }
  }

  void DatabaseManager::RollbackTransaction()
  {
    if (transaction_.get() == NULL)
    {
      LOG(INFO) << "Cannot rollback a non-existing transaction";
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      try
      {
        transaction_->Rollback();
        transaction_.reset(NULL);
      }
      catch (Orthanc::OrthancException &e)
      {
        CloseIfUnavailable(e.GetErrorCode());
        throw;
      }
    }
  }

  DatabaseManager::Transaction::Transaction(DatabaseManager &manager,
                                            TransactionType type) : manager_(manager),
                                                                    database_(manager.GetDatabase()),
                                                                    active_(true)
  {
    manager_.StartTransaction(type);
  }

  DatabaseManager::Transaction::~Transaction()
  {
    if (active_)
    {
      try
      {
        manager_.RollbackTransaction();
      }
      catch (Orthanc::OrthancException &e)
      {
        // Don't rethrow the exception as we are in a destructor
        LOG(ERROR) << "Uncatched error during some transaction rollback: " << e.What();
      }
    }
  }

  void DatabaseManager::Transaction::Commit()
  {
    if (active_)
    {
      manager_.CommitTransaction();
      active_ = false;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }

  void DatabaseManager::Transaction::Rollback()
  {
    if (active_)
    {
      manager_.RollbackTransaction();
      active_ = false;
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
  }

}
