// Taken from https://hg.orthanc-server.com/orthanc-databases/


#pragma once

#if ORTHANC_BUILDING_SERVER_LIBRARY == 1
#  include "../../../OrthancFramework/Sources/Enumerations.h"
#else
#  include <Enumerations.h>
#endif

#include <boost/noncopyable.hpp>
#include <vector>

namespace Orthanc
{
  class DatabaseConstraint;

  // This class is also used by the "orthanc-databases" project
  class ISqlLookupFormatter : public boost::noncopyable
  {
  public:
    virtual ~ISqlLookupFormatter()
    {
    }

    virtual std::string GenerateParameter(const std::string& value) = 0;

    virtual std::string FormatResourceType(ResourceType level) = 0;

    virtual std::string FormatWildcardEscape() = 0;

    /**
     * Whether to escape '[' and ']', which is only needed for
     * MSSQL. New in Orthanc 1.9.8, from the following changeset:
     * https://hg.orthanc-server.com/orthanc-databases/rev/389c037387ea
     **/
    virtual bool IsEscapeBrackets() const = 0;

    static void Apply(std::string& sql,
                      ISqlLookupFormatter& formatter,
                      const std::vector<DatabaseConstraint>& lookup,
                      ResourceType queryLevel,
                      size_t limit);
  };
}
