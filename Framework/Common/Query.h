// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "DatabasesEnumerations.h"

#include <map>
#include <vector>
#include <string>
#include <boost/noncopyable.hpp>


namespace OrthancDatabases
{
  class Query : public boost::noncopyable
  {
  public:
    class IParameterFormatter : public boost::noncopyable
    {
    public:
      virtual ~IParameterFormatter()
      {
      }

      virtual void Format(std::string& target,
                          const std::string& source,
                          ValueType type) = 0;
    };

  private:
    typedef std::map<std::string, ValueType>  Parameters;

    class Token;

    std::vector<Token*>  tokens_;
    Parameters           parameters_;
    bool                 readOnly_;

    void Setup(const std::string& sql);

  public:
    explicit Query(const std::string& sql);

    Query(const std::string& sql,
          bool isReadOnly);

    ~Query();

    bool IsReadOnly() const
    {
      return readOnly_;
    }

    void SetReadOnly(bool isReadOnly)
    {
      readOnly_ = isReadOnly;
    }

    bool HasParameter(const std::string& parameter) const;

    ValueType GetType(const std::string& parameter) const;

    void SetType(const std::string& parameter,
                 ValueType type);

    void Format(std::string& result,
                IParameterFormatter& formatter) const;
  };
}
