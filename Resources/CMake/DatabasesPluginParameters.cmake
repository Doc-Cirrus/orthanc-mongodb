# Taken from https://hg.orthanc-server.com/orthanc-databases/

set(ALLOW_DOWNLOADS OFF CACHE BOOL "Allow CMake to download packages")
set(ORTHANC_FRAMEWORK_SOURCE "${ORTHANC_FRAMEWORK_DEFAULT_SOURCE}" CACHE STRING "Source of the Orthanc source code (can be \"hg\", \"archive\", \"web\" or \"path\")")
set(ORTHANC_FRAMEWORK_ARCHIVE "" CACHE STRING "Path to the Orthanc archive, if ORTHANC_FRAMEWORK_SOURCE is \"archive\"")
set(ORTHANC_FRAMEWORK_ROOT "" CACHE STRING "Path to the Orthanc source directory, if ORTHANC_FRAMEWORK_SOURCE is \"path\"")

# Advanced parameters to fine-tune linking against system libraries
set(USE_SYSTEM_ORTHANC_SDK ON CACHE BOOL "Use the system version of the Orthanc plugin SDK")
set(ORTHANC_SDK_VERSION "1.9.2" CACHE STRING "Version of the Orthanc plugin SDK to use, if not using the system version (can be \"0.9.5\", \"1.4.0\", \"1.5.2\", \"1.5.4\", \"1.9.2\" or \"framework\")")

include(${CMAKE_CURRENT_LIST_DIR}/DatabasesFrameworkParameters.cmake)

set(ENABLE_GOOGLE_TEST ON)
