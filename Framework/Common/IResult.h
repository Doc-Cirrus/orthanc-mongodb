// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IValue.h"

namespace OrthancDatabases
{
  class IResult : public boost::noncopyable
  {
  public:
    virtual ~IResult()
    {
    }

    virtual void SetExpectedType(size_t field,
                                 ValueType type) = 0;

    virtual bool IsDone() const = 0;

    virtual void Next() = 0;

    virtual size_t GetFieldsCount() const = 0;

    virtual const IValue& GetField(size_t index) const = 0;

    static void Print(std::ostream& stream,
                      IResult& result);
  };
}
