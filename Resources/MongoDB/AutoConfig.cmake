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

# Downloads, builds and installs the MongoDB C and C++ drivers inside
# the build directory, at configuration time. Afterwards, the drivers
# are found through "find_package()" like system-wide installations
# (see "MongoDBConfiguration.cmake").

# Common variables
set(BUILD_DIR_POSTFIX "-build")
set(INSTALL_DIR_POSTFIX "-install")
set(CONFIGURATION_DIR_POSTFIX "-config")
set(MONGO_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/mongo-downloads")

# Mongo C Driver variables
set(MONGO_C_PROJECT     "mongo-c-driver")
set(MONGO_C_VERSION     "2.5.4")
set(MONGO_C_SHA256      "9ddca33cfad97af34f264895f0e9c687d851f5e12f951ecb5794f95e9f5c5fb3")
set(MONGO_C_SOURCE_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}")
set(MONGO_C_BINARY_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${BUILD_DIR_POSTFIX}")
set(MONGO_C_INSTALL_DIR "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${INSTALL_DIR_POSTFIX}")
set(MONGO_C_CONFIG_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${CONFIGURATION_DIR_POSTFIX}")

# Mongo CXX Driver variables
set(MONGO_CXX_PROJECT     "mongo-cxx-driver")
set(MONGO_CXX_VERSION     "4.6.0")
set(MONGO_CXX_SHA256      "eac122db0789fc82b0ba93f92a1503d74c502bfe4728345eaa8650e50a79da11")
set(MONGO_CXX_SOURCE_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}")
set(MONGO_CXX_BINARY_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${BUILD_DIR_POSTFIX}")
set(MONGO_CXX_INSTALL_DIR "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${INSTALL_DIR_POSTFIX}")
set(MONGO_CXX_CONFIG_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${CONFIGURATION_DIR_POSTFIX}")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_UPPERCASE)

if (STATIC_BUILD OR LINK_STATIC_LIBS)
    set(MONGO_ENABLE_STATIC ON)
    set(MONGO_ENABLE_SHARED OFF)
else ()
    set(MONGO_ENABLE_STATIC OFF)
    set(MONGO_ENABLE_SHARED ON)
endif ()

include(ProcessorCount)
ProcessorCount(MONGO_BUILD_JOBS)
if (MONGO_BUILD_JOBS EQUAL 0)
    set(MONGO_BUILD_JOBS 1)
endif()

# Macros
macro(CheckError RESULT)
    if(${RESULT})
        message(FATAL_ERROR "Failed to build project: ${RESULT}")
    endif()
endmacro()

macro(InstallPackage PROJECT_WORKING_DIR)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
        RESULT_VARIABLE RESULT
        WORKING_DIRECTORY ${PROJECT_WORKING_DIR}
    )

    CheckError(RESULT)

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE} --parallel ${MONGO_BUILD_JOBS}
        RESULT_VARIABLE RESULT
        WORKING_DIRECTORY ${PROJECT_WORKING_DIR}
    )

    CheckError(RESULT)
endmacro()

IF (MSVC AND STATIC_BUILD)
    # Link statically against MSVC's runtime libraries
    set(CompilerFlags
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
    )
    foreach(CompilerFlag ${CompilerFlags})
        string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
        string(REPLACE "/MDd" "/MTd" ${CompilerFlag} "${${CompilerFlag}}")
    endforeach()
ENDIF ()


# Install mongo-c-driver
message(STATUS "Building mongo-c-driver ${MONGO_C_VERSION} into ${MONGO_C_INSTALL_DIR}")

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/${MONGO_C_PROJECT}.txt.in
    ${MONGO_C_CONFIG_DIR}/CMakeLists.txt)

InstallPackage(${MONGO_C_CONFIG_DIR})


# Install mongo-cxx-driver
message(STATUS "Building mongo-cxx-driver ${MONGO_CXX_VERSION} into ${MONGO_CXX_INSTALL_DIR}")

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/${MONGO_CXX_PROJECT}.txt.in
    ${MONGO_CXX_CONFIG_DIR}/CMakeLists.txt)

InstallPackage(${MONGO_CXX_CONFIG_DIR})


# Make the freshly installed drivers visible to "find_package()"
list(INSERT CMAKE_PREFIX_PATH 0 "${MONGO_CXX_INSTALL_DIR}" "${MONGO_C_INSTALL_DIR}")

if (MONGO_ENABLE_SHARED)
    # Set runtime path for the shared libraries
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    set(CMAKE_INSTALL_RPATH "${MONGO_C_INSTALL_DIR}/lib;${MONGO_CXX_INSTALL_DIR}/lib")
endif()

# Include boost headers in case if boost used
IF (BOOST_ROOT)
    include_directories(${BOOST_ROOT})
ENDIF ()
