// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once


namespace OrthancDatabases
{
  enum ValueType
  {
    ValueType_BinaryString,
    ValueType_InputFile,
    ValueType_Integer64,
    ValueType_Null,
    ValueType_ResultFile,
    ValueType_Utf8String
  };

  enum Dialect
  {
    Dialect_MySQL,
    Dialect_PostgreSQL,
    Dialect_SQLite,
    Dialect_MSSQL,
    Dialect_Unknown
  };

  enum TransactionType
  {
    TransactionType_ReadWrite,
    TransactionType_ReadOnly,  // Should only arise with Orthanc SDK >= 1.9.2 in the index plugin
    TransactionType_Implicit   // Should only arise with Orthanc SDK <= 1.9.1
  };

  const char* EnumerationToString(ValueType type);
}
