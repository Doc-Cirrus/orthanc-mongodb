# Taken from https://hg.orthanc-server.com/orthanc-databases/

#####################################################################
## Import the parameters of the Orthanc Framework
#####################################################################

include(${CMAKE_CURRENT_LIST_DIR}/../Orthanc/CMake/DownloadOrthancFramework.cmake)

if (NOT ORTHANC_FRAMEWORK_SOURCE STREQUAL "system")
  include(${ORTHANC_FRAMEWORK_ROOT}/../Resources/CMake/OrthancFrameworkParameters.cmake)
endif()
