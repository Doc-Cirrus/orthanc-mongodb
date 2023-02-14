// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "Query.h"

#include <Compatibility.h>

#include <set>

namespace OrthancDatabases
{
  class GenericFormatter : public Query::IParameterFormatter
  {
  private:
    Dialect                   autoincrementDialect_;
    Dialect                   namedDialect_;
    std::vector<std::string>  parametersName_;
    std::vector<ValueType>    parametersType_;

  public:
    explicit GenericFormatter(Dialect dialect) :
      autoincrementDialect_(dialect),
      namedDialect_(dialect)
    {
    }

    Dialect GetDialect() const;

    Dialect GetAutoincrementDialect() const
    {
      return autoincrementDialect_;
    }

    void SetAutoincrementDialect(Dialect dialect)
    {
      autoincrementDialect_ = dialect;
    }

    Dialect GetNamedDialect() const
    {
      return namedDialect_;
    }

    void SetNamedDialect(Dialect dialect)
    {
      namedDialect_ = dialect;
    }

    virtual void Format(std::string& target,
                        const std::string& source,
                        ValueType type) ORTHANC_OVERRIDE;

    size_t GetParametersCount() const
    {
      return parametersName_.size();
    }

    const std::string& GetParameterName(size_t index) const;

    ValueType GetParameterType(size_t index) const;
  };
}
