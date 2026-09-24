/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#include "PluginInitialization.h"

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
                        const std::string& pluginName,
                        const std::string& dbms,
                        bool isIndex)
  {
#if ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 12, 4)
    Orthanc::Logging::InitializePluginContext(context, pluginName);
#elif ORTHANC_FRAMEWORK_VERSION_IS_ABOVE(1, 7, 2)
    Orthanc::Logging::InitializePluginContext(context);
#else
    Orthanc::Logging::Initialize(context);
#endif

    Orthanc::Logging::EnableInfoLevel(true);
    OrthancPlugins::SetGlobalContext(context);

    assert(DisplayPerformanceWarning(dbms, isIndex));

    /* Check the version of the Orthanc core */
    if (OrthancPluginCheckVersion(context) == 0)
    {
      LOG(ERROR) << "Your version of Orthanc ("
                 << context->orthancVersion << ") must be above "
                 << ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER << "."
                 << ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER << "."
                 << ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER
                 << " to run this plugin";
      return false;
    }


    /* Warn the user if the Orthanc runtime has not an optimal version */

    bool useFallback = true;
    bool isOptimal = false;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)         // Macro introduced in Orthanc 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 4, 0)
    // MongoDB: only the V4 database API is implemented (upstream: 0.9.5)
    if (OrthancPluginCheckVersionAdvanced(context, 1, 13, 0) == 0)
    {
      LOG(ERROR) << "Your version of Orthanc (" << context->orthancVersion 
                 << ") must be above 1.13.0 to run this plugin";
      return false;
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
    
    OrthancPlugins::SetDescription(pluginName, description);

    return true;
  }
}
