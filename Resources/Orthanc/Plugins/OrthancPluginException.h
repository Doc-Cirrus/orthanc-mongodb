// Taken from https://hg.orthanc-server.com/orthanc-databases/


#pragma once

#if !defined(HAS_ORTHANC_EXCEPTION)
#  error The macro HAS_ORTHANC_EXCEPTION must be defined
#endif


#if HAS_ORTHANC_EXCEPTION == 1
#  include <OrthancException.h>
#  define ORTHANC_PLUGINS_ERROR_ENUMERATION     ::Orthanc::ErrorCode
#  define ORTHANC_PLUGINS_EXCEPTION_CLASS       ::Orthanc::OrthancException
#  define ORTHANC_PLUGINS_GET_ERROR_CODE(code)  ::Orthanc::ErrorCode_ ## code
#else
#  include <orthanc/OrthancCPlugin.h>
#  define ORTHANC_PLUGINS_ERROR_ENUMERATION     ::OrthancPluginErrorCode
#  define ORTHANC_PLUGINS_EXCEPTION_CLASS       ::OrthancPlugins::PluginException
#  define ORTHANC_PLUGINS_GET_ERROR_CODE(code)  ::OrthancPluginErrorCode_ ## code
#endif


#define ORTHANC_PLUGINS_THROW_PLUGIN_ERROR_CODE(code)                   \
  throw ORTHANC_PLUGINS_EXCEPTION_CLASS(static_cast<ORTHANC_PLUGINS_ERROR_ENUMERATION>(code));


#define ORTHANC_PLUGINS_THROW_EXCEPTION(code)                           \
  throw ORTHANC_PLUGINS_EXCEPTION_CLASS(ORTHANC_PLUGINS_GET_ERROR_CODE(code));


#define ORTHANC_PLUGINS_CHECK_ERROR(code)                           \
  if (code != ORTHANC_PLUGINS_GET_ERROR_CODE(Success))              \
  {                                                                 \
    ORTHANC_PLUGINS_THROW_EXCEPTION(code);                          \
  }


namespace OrthancPlugins
{
#if HAS_ORTHANC_EXCEPTION == 0
  class PluginException
  {
  private:
    OrthancPluginErrorCode  code_;

  public:
    explicit PluginException(OrthancPluginErrorCode code) : code_(code)
    {
    }

    OrthancPluginErrorCode GetErrorCode() const
    {
      return code_;
    }

    const char* What(OrthancPluginContext* context) const
    {
      const char* description = OrthancPluginGetErrorDescription(context, code_);
      if (description)
      {
        return description;
      }
      else
      {
        return "No description available";
      }
    }
  };
#endif
}
