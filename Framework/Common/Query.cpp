// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "Query.h"

#include <Logging.h>
#include <OrthancException.h>

#include <boost/regex.hpp>

namespace OrthancDatabases
{
  class Query::Token : public boost::noncopyable
  {
  private:
    bool         isParameter_;
    std::string  content_;

  public:
    Token(bool isParameter,
          const std::string& content) :
      isParameter_(isParameter),
      content_(content)
    {
    }

    bool IsParameter() const
    {
      return isParameter_;
    }

    const std::string& GetContent() const
    {
      return content_;
    }
  };


  void Query::Setup(const std::string& sql)
  {
    boost::regex regex("\\$\\{(.*?)\\}");

    std::string::const_iterator last = sql.begin();
    boost::sregex_token_iterator it(sql.begin(), sql.end(), regex, 0);
    boost::sregex_token_iterator end;

    while (it != end)
    {
      if (last != it->first)
      {
        tokens_.push_back(new Token(false, std::string(last, it->first)));
      }

      std::string parameter = *it;
      assert(parameter.size() >= 3);
      parameter = parameter.substr(2, parameter.size() - 3);

      tokens_.push_back(new Token(true, parameter));
      parameters_[parameter] = ValueType_Null;

      last = it->second;

      ++it;
    }

    if (last != sql.end())
    {
      tokens_.push_back(new Token(false, std::string(last, sql.end())));
    }
  }


  Query::Query(const std::string& sql) :
    readOnly_(false)
  {
    Setup(sql);
  }


  Query::Query(const std::string& sql,
               bool readOnly) :
    readOnly_(readOnly)
  {
    Setup(sql);
  }


  Query::~Query()
  {
    for (size_t i = 0; i < tokens_.size(); i++)
    {
      assert(tokens_[i] != NULL);
      delete tokens_[i];
    }
  }


  bool Query::HasParameter(const std::string& parameter) const
  {
    return parameters_.find(parameter) != parameters_.end();
  }


  ValueType Query::GetType(const std::string& parameter) const
  {
    Parameters::const_iterator found = parameters_.find(parameter);

    if (found == parameters_.end())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentItem,
                                      "Inexistent parameter in a SQL query: " + parameter);
    }
    else
    {
      return found->second;
    }
  }


  void Query::SetType(const std::string& parameter,
                      ValueType type)
  {
    Parameters::iterator found = parameters_.find(parameter);

    if (found == parameters_.end())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_InexistentItem,
                                      "Inexistent parameter in a SQL query: " + parameter);
    }
    else
    {
      found->second = type;
    }
  }


  void Query::Format(std::string& result,
                     IParameterFormatter& formatter) const
  {
    result.clear();

    for (size_t i = 0; i < tokens_.size(); i++)
    {
      assert(tokens_[i] != NULL);

      const std::string& content = tokens_[i]->GetContent();

      if (tokens_[i]->IsParameter())
      {
        std::string parameter;
        formatter.Format(parameter, content, GetType(content));
        result += parameter;
      }
      else
      {
        result += content;
      }
    }
  }
}
