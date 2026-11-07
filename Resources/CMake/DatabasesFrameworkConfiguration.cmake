# MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
# Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
# Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
# Department, University Hospital of Liege, Belgium
# Copyright (C) 2017-2023 Osimis S.A., Belgium
# Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
# Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


#####################################################################
## Configure the Orthanc Framework
#####################################################################

if (ORTHANC_FRAMEWORK_SOURCE STREQUAL "system")
  if (ORTHANC_FRAMEWORK_USE_SHARED)
    # https://cmake.org/cmake/help/latest/policy/CMP0167.html
    if (CMAKE_VERSION VERSION_GREATER "3.30")
      find_package(Boost CONFIG)
    else()
      include(FindBoost)
    endif()

    find_package(Boost COMPONENTS regex thread)

    if (NOT Boost_FOUND)
      message(FATAL_ERROR "Unable to locate Boost on this system")
    endif()

    link_libraries(${Boost_LIBRARIES} jsoncpp)
  endif()

  link_libraries(${ORTHANC_FRAMEWORK_LIBRARIES})

  # These parameters should *NOT* be modified by the user: System-wide
  # installations expect only dynamic linking
  set(USE_SYSTEM_GOOGLE_TEST ON CACHE BOOL "Use the system version of Google Test")
  set(USE_SYSTEM_OPENSSL ON CACHE BOOL "Use the system version of OpenSSL")
  set(USE_SYSTEM_PROTOBUF ON CACHE BOOL "Use the system version of Google Protocol Buffers")

  set(USE_GOOGLE_TEST_DEBIAN_PACKAGE OFF CACHE BOOL "Use the sources of Google Test shipped with libgtest-dev (Debian only)")
  mark_as_advanced(USE_GOOGLE_TEST_DEBIAN_PACKAGE)

  include(${CMAKE_CURRENT_LIST_DIR}/../Orthanc/CMake/GoogleTestConfiguration.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/../Orthanc/CMake/ProtobufConfiguration.cmake)

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
  ${ORTHANC_DATABASES_ROOT}/Framework/Common/RetryDatabaseFactory.cpp
  )


#####################################################################
## Configure the MongoDB drivers
#####################################################################

if (ENABLE_MONGODB_BACKEND)
  include(${CMAKE_CURRENT_LIST_DIR}/MongoDBConfiguration.cmake)
  add_definitions(-DORTHANC_ENABLE_MONGODB=1)
  list(APPEND DATABASES_SOURCES
    ${ORTHANC_DATABASES_ROOT}/Framework/MongoDB/MongoDBCollection.cpp
    ${ORTHANC_DATABASES_ROOT}/Framework/MongoDB/MongoDBDatabase.cpp
    ${ORTHANC_DATABASES_ROOT}/Framework/MongoDB/MongoDBParameters.cpp
    ${ORTHANC_DATABASES_ROOT}/Framework/MongoDB/MongoDBToolbox.cpp
    ${ORTHANC_DATABASES_ROOT}/Framework/MongoDB/MongoDBTransaction.cpp
    )
else()
  add_definitions(-DORTHANC_ENABLE_MONGODB=0)
endif()
