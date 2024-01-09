# Taken from https://hg.orthanc-server.com/orthanc-databases/


#####################################################################
## Configure the Orthanc Framework
#####################################################################
if (ORTHANC_FRAMEWORK_SOURCE STREQUAL "system")
  if (ORTHANC_FRAMEWORK_USE_SHARED)
    include(FindBoost)
    find_package(Boost COMPONENTS regex thread)

    if (NOT Boost_FOUND)
      message(FATAL_ERROR "Unable to locate Boost on this system")
    endif()

    link_libraries(${Boost_LIBRARIES} jsoncpp)
  endif()

  link_libraries(${ORTHANC_FRAMEWORK_LIBRARIES})

  if (ENABLE_SQLITE_BACKEND)
    add_definitions(-DORTHANC_ENABLE_SQLITE=1)
  endif()

  set(USE_SYSTEM_GOOGLE_TEST ON CACHE BOOL "Use the system version of Google Test")
  set(USE_GOOGLE_TEST_DEBIAN_PACKAGE OFF CACHE BOOL "Use the sources of Google Test shipped with libgtest-dev (Debian only)")
  mark_as_advanced(USE_GOOGLE_TEST_DEBIAN_PACKAGE)
  include(${CMAKE_CURRENT_LIST_DIR}/../Orthanc/CMake/GoogleTestConfiguration.cmake)

else()
  # Those modules of the Orthanc framework are not needed when dealing
  # with databases
  set(ENABLE_MODULE_IMAGES OFF)
  set(ENABLE_MODULE_JOBS OFF)
  set(ENABLE_MODULE_DICOM OFF)

  include(${ORTHANC_FRAMEWORK_ROOT}/../Resources/CMake/OrthancFrameworkConfiguration.cmake)
  include_directories(${ORTHANC_FRAMEWORK_ROOT})
endif()



#####################################################################
## Common source files for the databases
#####################################################################

set(ORTHANC_DATABASES_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

set(DATABASES_SOURCES
  ${ORTHANC_DATABASES_ROOT}/Framework/Common/DatabaseManager.cpp
  ${ORTHANC_DATABASES_ROOT}/Framework/Common/DatabasesEnumerations.cpp
  ${ORTHANC_DATABASES_ROOT}/Framework/MongoDB/MongoDatabase.cpp
  )

#####################################################################
## Configure MongoDB
#####################################################################
add_definitions(-DORTHANC_ENABLE_MONGODB=1)
include(${CMAKE_CURRENT_LIST_DIR}/MongoDBConfiguration.cmake)
