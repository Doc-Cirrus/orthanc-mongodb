// Taken from https://hg.orthanc-server.com/orthanc-databases/

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

  DatabaseManager::DatabaseManager(IDatabaseFactory *factory) : factory_(factory)
  {
    if (factory == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }
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
