// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "IResult.h"

#include "Utf8StringValue.h"

#include <Compatibility.h>
#include <OrthancException.h>

#include <cassert>
#include <list>
#include <vector>
#include <ostream>

namespace OrthancDatabases
{
  static void PrintSeparators(std::ostream& stream,
                              char c,
                              size_t count)
  {
    for (size_t i = 0; i < count; i++)
    {
      stream << c;
    }
  }


  static void PrintHeader(std::ostream& stream,
                          const std::vector<size_t>& maxWidth)
  {
    for (size_t i = 0; i < maxWidth.size(); i++)
    {
      stream << '+';
      PrintSeparators(stream, '-', maxWidth[i] + 2);
    }

    stream << '+' << std::endl;
  }


  void IResult::Print(std::ostream& stream,
                      IResult& result)
  {
    typedef std::list< std::vector<std::string> > Table;

    Table table;

    const size_t columns = result.GetFieldsCount();

    std::vector<size_t> maxWidth(columns);

    while (!result.IsDone())
    {
      table.push_back(std::vector<std::string>(columns));

      for (size_t i = 0; i < columns; i++)
      {
        std::string value;

        try
        {
          std::unique_ptr<IValue> converted(
            result.GetField(i).Convert(ValueType_Utf8String));
          value = dynamic_cast<Utf8StringValue&>(*converted).GetContent();
        }
        catch (Orthanc::OrthancException&)
        {
          value = "?";
        }

        if (value.size() > maxWidth[i])
        {
          maxWidth[i] = value.size();
        }

        table.back() [i] = value;
      }

      result.Next();
    }

    PrintHeader(stream, maxWidth);

    for (Table::const_iterator it = table.begin(); it != table.end(); ++it)
    {
      assert(it->size() == maxWidth.size());

      for (size_t i = 0; i < it->size(); i++)
      {
        const std::string& value = (*it) [i];

        stream << "| " << value << ' ';

        for (size_t j = value.size(); j < maxWidth[i]; j++)
        {
          stream << ' ';
        }
      }

      stream << '|' << std::endl;
    }

    PrintHeader(stream, maxWidth);
  }
}
