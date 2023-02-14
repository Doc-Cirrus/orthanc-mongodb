// Taken from https://hg.orthanc-server.com/orthanc-databases/

#include "PluginInitialization.h"

#include "../Common/ImplicitTransaction.h"
#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#include <Logging.h>
#include <Toolbox.h>


namespace OrthancDatabases
{
  static bool DisplayPerformanceWarning(const std::string& dbms,
                                        bool isIndex)
  {
    (void) DisplayPerformanceWarning;   // Disable warning about unused function
    LOG(WARNING) << "Performance warning in " << dbms
                 << (isIndex ? " index" : " storage area")
                 << ": Non-release build, runtime debug assertions are turned on";
    return true;
  }


  bool InitializePlugin(OrthancPluginContext* context,
                        const std::string& dbms,
                        bool isIndex)
  {
#if ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 7, 2)
    Orthanc::Logging::InitializePluginContext(context);
#else
    Orthanc::Logging::Initialize(context);
#endif

    Orthanc::Logging::EnableInfoLevel(true);
    OrthancPlugins::SetGlobalContext(context);
    ImplicitTransaction::SetErrorOnDoubleExecution(false);

    assert(DisplayPerformanceWarning(dbms, isIndex));

    /* Check the version of the Orthanc core */

    bool useFallback = true;
    bool isOptimal = false;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)         // Macro introduced in Orthanc 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 4, 0)
    if (OrthancPluginCheckVersionAdvanced(context, 0, 9, 5) == 0)
    {
      LOG(ERROR) << "Your version of Orthanc (" << context->orthancVersion
                 << ") must be above 0.9.5 to run this plugin";
      return false;
    }

    if (OrthancPluginCheckVersionAdvanced(context, 1, 4, 0) == 1)
    {
      ImplicitTransaction::SetErrorOnDoubleExecution(true);
    }

    if (OrthancPluginCheckVersionAdvanced(context,
                                          ORTHANC_OPTIMAL_VERSION_MAJOR,
                                          ORTHANC_OPTIMAL_VERSION_MINOR,
                                          ORTHANC_OPTIMAL_VERSION_REVISION) == 1)
    {
      isOptimal = true;
    }

    useFallback = false;
#  endif
#endif

    if (useFallback &&
        OrthancPluginCheckVersion(context) == 0)
    {
      LOG(ERROR) << "Your version of Orthanc ("
                 << context->orthancVersion << ") must be above "
                 << ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER << "."
                 << ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER << "."
                 << ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER
                 << " to run this plugin";
      return false;
    }

    if (useFallback)
    {
      std::string v(context->orthancVersion);

      if (v == "mainline")
      {
        isOptimal = true;
      }
      else
      {
        std::vector<std::string> tokens;
        Orthanc::Toolbox::TokenizeString(tokens, v, '.');

        if (tokens.size() != 3)
        {
          LOG(ERROR) << "Bad version of Orthanc: " << v;
          return false;
        }

        int major = boost::lexical_cast<int>(tokens[0]);
        int minor = boost::lexical_cast<int>(tokens[1]);
        int revision = boost::lexical_cast<int>(tokens[2]);

        isOptimal = (major > ORTHANC_OPTIMAL_VERSION_MAJOR ||
                     (major == ORTHANC_OPTIMAL_VERSION_MAJOR &&
                      minor > ORTHANC_OPTIMAL_VERSION_MINOR) ||
                     (major == ORTHANC_OPTIMAL_VERSION_MAJOR &&
                      minor == ORTHANC_OPTIMAL_VERSION_MINOR &&
                      revision >= ORTHANC_OPTIMAL_VERSION_REVISION));
      }
    }

    if (!isOptimal &&
        isIndex)
    {
      LOG(WARNING) << "Performance warning in " << dbms
                   << " index: Your version of Orthanc ("
                   << context->orthancVersion << ") should be upgraded to "
                   << ORTHANC_OPTIMAL_VERSION_MAJOR << "."
                   << ORTHANC_OPTIMAL_VERSION_MINOR << "."
                   << ORTHANC_OPTIMAL_VERSION_REVISION
                   << " to benefit from best performance";
    }


    std::string description = ("Stores the Orthanc " +
                               std::string(isIndex ? "index" : "storage area") +
                               " into a " + dbms + " database");

    OrthancPluginSetDescription(context, description.c_str());

    return true;
  }
}
