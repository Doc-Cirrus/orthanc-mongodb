// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IDatabaseFactory.h"
#include "StatementLocation.h"

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
    typedef std::map<StatementLocation, IPrecompiledStatement*>  CachedStatements;

    std::unique_ptr<IDatabaseFactory>  factory_;
    std::unique_ptr<IDatabase>     database_;
    std::unique_ptr<ITransaction>  transaction_;
    CachedStatements               cachedStatements_;
    Dialect                        dialect_;

    void CloseIfUnavailable(Orthanc::ErrorCode e);

    IPrecompiledStatement* LookupCachedStatement(const StatementLocation& location) const;

    IPrecompiledStatement& CacheStatement(const StatementLocation& location,
                                          const Query& query);

    ITransaction& GetTransaction();

    void ReleaseImplicitTransaction();

  public:
    explicit DatabaseManager(IDatabaseFactory* factory);  // Takes ownership

    ~DatabaseManager()
    {
      Close();
    }

    IDatabase& GetDatabase();

    Dialect GetDialect() const;

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


    class StatementBase : public boost::noncopyable
    {
    private:
      DatabaseManager&          manager_;
      ITransaction&             transaction_;
      std::unique_ptr<Query>    query_;
      std::unique_ptr<IResult>  result_;

      IResult& GetResult() const;

    protected:
      DatabaseManager& GetManager() const
      {
        return manager_;
      }

      ITransaction& GetTransaction() const
      {
        return transaction_;
      }

      void SetQuery(Query* query);

      void SetResult(IResult* result);

      void ClearResult()
      {
        result_.reset();
      }

      Query* ReleaseQuery()
      {
        return query_.release();
      }

    public:
      explicit StatementBase(DatabaseManager& manager);

      virtual ~StatementBase();

      // Used only by SQLite
      IDatabase& GetDatabase()
      {
        return manager_.GetDatabase();
      }

      void SetReadOnly(bool readOnly);

      void SetParameterType(const std::string& parameter,
                            ValueType type);

      bool IsDone() const;

      void Next();

      size_t GetResultFieldsCount() const;

      void SetResultFieldType(size_t field,
                              ValueType type);

      const IValue& GetResultField(size_t index) const;

      int32_t ReadInteger32(size_t field) const;

      int64_t ReadInteger64(size_t field) const;

      std::string ReadString(size_t field) const;

      void PrintResult(std::ostream& stream)
      {
        IResult::Print(stream, GetResult());
      }
    };


    /**
     * WARNING: At any given time, there must be at most 1 object of
     * the "CachedStatement" class in the scope, otherwise error
     * "Cannot execute more than one statement in an implicit
     * transaction" is generated if no explicit transaction is
     * present.
     **/
    class CachedStatement : public StatementBase
    {
    private:
      StatementLocation       location_;
      IPrecompiledStatement*  statement_;

    public:
      CachedStatement(const StatementLocation& location,
                      DatabaseManager& manager,
                      const std::string& sql);

      void Execute()
      {
        Dictionary parameters;
        Execute(parameters);
      }

      void Execute(const Dictionary& parameters);
    };


    class StandaloneStatement : public StatementBase
    {
    private:
      std::unique_ptr<IPrecompiledStatement>  statement_;

    public:
      StandaloneStatement(DatabaseManager& manager,
                          const std::string& sql);

      virtual ~StandaloneStatement();

      void Execute()
      {
        Dictionary parameters;
        Execute(parameters);
      }

      void Execute(const Dictionary& parameters);
    };
  };
}
