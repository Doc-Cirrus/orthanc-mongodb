// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "ITransaction.h"

#include <Compatibility.h>

namespace OrthancDatabases
{
  class ImplicitTransaction : public ITransaction
  {
  private:
    enum State
    {
      State_Ready,
      State_Executed,
      State_Committed
    };

    State   state_;

    void CheckStateForExecution();

  protected:
    virtual IResult* ExecuteInternal(IPrecompiledStatement& statement,
                                     const Dictionary& parameters) = 0;

    virtual void ExecuteWithoutResultInternal(IPrecompiledStatement& statement,
                                              const Dictionary& parameters) = 0;

  public:
    ImplicitTransaction();

    virtual ~ImplicitTransaction();

    virtual bool IsImplicit() const ORTHANC_OVERRIDE
    {
      return true;
    }

    virtual void Rollback() ORTHANC_OVERRIDE;

    virtual void Commit() ORTHANC_OVERRIDE;

    virtual IResult* Execute(IPrecompiledStatement& statement,
                             const Dictionary& parameters) ORTHANC_OVERRIDE;

    virtual void ExecuteWithoutResult(IPrecompiledStatement& statement,
                                      const Dictionary& parameters) ORTHANC_OVERRIDE;

    static void SetErrorOnDoubleExecution(bool isError);

    static bool IsErrorOnDoubleExecution();
  };
}
