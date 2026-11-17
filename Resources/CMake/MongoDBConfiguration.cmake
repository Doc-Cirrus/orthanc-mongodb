#
# MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
# Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
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
#

# Locates the MongoDB C driver (>= 2.0) and C++ driver (>= 4.0), either
# system-wide, below MONGOC_ROOT/MONGOCXX_ROOT, or built by AutoConfig.cmake
# if AUTO_INSTALL_DEPENDENCIES is set. The result is the MONGODB_LIBS
# variable, which contains the imported CMake targets of the drivers.

set(MONGO_C_MINIMAL_VERSION "2.0")
set(MONGO_CXX_MINIMAL_VERSION "4.0")

if (PORTABLE_BUILD)
  if (NOT STATIC_BUILD OR NOT AUTO_INSTALL_DEPENDENCIES)
    message(FATAL_ERROR "PORTABLE_BUILD needs STATIC_BUILD and AUTO_INSTALL_DEPENDENCIES")
  endif()

  # Found by the CMake package of the static C driver
  set(OPENSSL_USE_STATIC_LIBS ON)

  # Only glibc remains a shared library (see Resources/CheckPortability.sh)
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
endif()

if (AUTO_INSTALL_DEPENDENCIES)
  include(${CMAKE_CURRENT_LIST_DIR}/../MongoDB/AutoConfig.cmake)
endif()

if (MONGOC_ROOT)
  list(APPEND CMAKE_PREFIX_PATH "${MONGOC_ROOT}")
endif()

if (MONGOCXX_ROOT)
  list(APPEND CMAKE_PREFIX_PATH "${MONGOCXX_ROOT}")
endif()

find_package(mongoc ${MONGO_C_MINIMAL_VERSION} CONFIG REQUIRED)
find_package(mongocxx ${MONGO_CXX_MINIMAL_VERSION} CONFIG REQUIRED)

# The static mongocxx is always preferred, cf. below
set(MONGO_PREFER_STATIC ON)

if (TARGET mongo::mongocxx_static)
  set(MONGODB_LIBS mongo::mongocxx_static mongo::bsoncxx_static)
  add_definitions(-DBSONCXX_STATIC -DMONGOCXX_STATIC)
elseif (TARGET mongo::mongocxx_shared)
  # Each plugin creates its own "mongocxx::instance", which the shared
  # library allows only once per process: both plugins could not be loaded
  message(FATAL_ERROR "Only static builds are supported: the shared mongocxx library allows a single "
    "mongocxx::instance per process, so the index and the storage plugins cannot be loaded together. "
    "Provide the static mongocxx libraries, or set AUTO_INSTALL_DEPENDENCIES=ON to build them")
else()
  message(FATAL_ERROR "The mongocxx package does not provide any usable library target")
endif()

if ((MONGO_PREFER_STATIC OR NOT TARGET mongoc::shared) AND TARGET mongoc::static)
  list(APPEND MONGODB_LIBS mongoc::static)
  add_definitions(-DBSON_STATIC -DMONGOC_STATIC)
elseif (TARGET mongoc::shared)
  list(APPEND MONGODB_LIBS mongoc::shared)
else()
  message(FATAL_ERROR "The mongoc package does not provide any usable library target")
endif()

message(STATUS "MongoDB C driver:   ${mongoc_VERSION}")
message(STATUS "MongoDB C++ driver: ${mongocxx_VERSION}")
message(STATUS "MongoDB libraries:  ${MONGODB_LIBS}")

if (MSVC)
  list(APPEND MONGODB_LIBS RpcRT4.Lib)
elseif (NOT APPLE)
  list(APPEND MONGODB_LIBS pthread rt)

  # With STATIC_BUILD, the Orthanc framework builds its own copy of libuuid
  if (NOT STATIC_BUILD AND USE_SYSTEM_UUID)
    list(APPEND MONGODB_LIBS uuid)
  endif()
endif()

link_libraries(${MONGODB_LIBS})
