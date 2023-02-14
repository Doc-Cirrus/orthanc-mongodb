// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "IResult.h"

#include <Compatibility.h>

#include <vector>

namespace OrthancDatabases
{
  class ResultBase : public IResult
  {
  private:
    void ClearFields();

    void ConvertFields();

    std::vector<IValue*>   fields_;
    std::vector<ValueType> expectedType_;
    std::vector<bool>      hasExpectedType_;

  protected:
    virtual IValue* FetchField(size_t index) = 0;

    void FetchFields();

    void SetFieldsCount(size_t count);

  public:
    virtual ~ResultBase()
    {
      ClearFields();
    }

    virtual void SetExpectedType(size_t field,
                                 ValueType type) ORTHANC_OVERRIDE;

    virtual size_t GetFieldsCount() const ORTHANC_OVERRIDE
    {
      return fields_.size();
    }

    virtual const IValue& GetField(size_t index) const ORTHANC_OVERRIDE;
  };
}
