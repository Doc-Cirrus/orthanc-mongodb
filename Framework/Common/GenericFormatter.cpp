// Taken from https://hg.orthanc-server.com/orthanc-databases/


#include "GenericFormatter.h"

#include <OrthancException.h>

#include <boost/lexical_cast.hpp>

namespace OrthancDatabases
{
  Dialect GenericFormatter::GetDialect() const
  {
    if (autoincrementDialect_ != namedDialect_)
    {
      // The two dialects do not match because of a previous call to
      // SetAutoincrementDialect() or SetNamedDialect()
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      return namedDialect_;
    }
  }


  void GenericFormatter::Format(std::string& target,
                                const std::string& source,
                                ValueType type)
  {
    if (source.empty())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else if (source == "AUTOINCREMENT")
    {
      if (GetParametersCount() != 0)
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls,
                                        "The AUTOINCREMENT argument must always be the first");
      }

      switch (autoincrementDialect_)
      {
        case Dialect_PostgreSQL:
          target = "DEFAULT, ";
          break;

        case Dialect_MySQL:
        case Dialect_SQLite:
          target = "NULL, ";
          break;

        case Dialect_MSSQL:
          target.clear();  // The IDENTITY field must not be filled in MSSQL
          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
      }
    }
    else
    {
      switch (namedDialect_)
      {
        case Dialect_PostgreSQL:
          target = "$" + boost::lexical_cast<std::string>(parametersName_.size() + 1);
          break;

        case Dialect_MySQL:
        case Dialect_SQLite:
        case Dialect_MSSQL:
          target = "?";
          break;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_NotImplemented);
      }

      parametersName_.push_back(source);
      parametersType_.push_back(type);
    }
  }


  const std::string& GenericFormatter::GetParameterName(size_t index) const
  {
    if (index >= parametersName_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else
    {
      return parametersName_[index];
    }
  }


  ValueType GenericFormatter::GetParameterType(size_t index) const
  {
    if (index >= parametersType_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else
    {
      return parametersType_[index];
    }
  }
}
