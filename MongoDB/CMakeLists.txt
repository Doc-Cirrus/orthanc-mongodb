#
# MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
# Copyright (C) 2017 - 2023  (Doc Cirrus GmbH)
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

cmake_minimum_required(VERSION 3.2)
project(OrthancMongoDB)

set(ORTHANC_OPTIMAL_VERSION_MAJOR 1)
set(ORTHANC_OPTIMAL_VERSION_MINOR 11)
set(ORTHANC_OPTIMAL_VERSION_REVISION 3)

if (ORTHANC_PLUGIN_VERSION STREQUAL "mainline")
    set(ORTHANC_FRAMEWORK_VERSION "mainline")
    set(ORTHANC_FRAMEWORK_DEFAULT_SOURCE "hg")
else ()
    set(ORTHANC_FRAMEWORK_VERSION "1.11.3")
    set(ORTHANC_FRAMEWORK_DEFAULT_SOURCE "web")
endif ()

include(${CMAKE_SOURCE_DIR}/../Resources/CMake/DatabasesPluginParameters.cmake)

set(CMAKE_CXX_FLAGS '-fPIC')
set(USE_SYSTEM_OPENSSL ON)
set(ORTHANC_PLUGIN_VERSION ${ORTHANC_FRAMEWORK_VERSION})

include(${CMAKE_SOURCE_DIR}/../Resources/CMake/DatabasesPluginConfiguration.cmake)

add_library(OrthancMongoFramework STATIC
        ${DATABASES_SOURCES}
        ${ORTHANC_DATABASES_ROOT}/Framework/Plugins/PluginInitialization.cpp
        Plugins/MongoDBIndex.cpp
        Plugins/MongoDBStorageArea.cpp
)

set_target_properties(OrthancMongoFramework PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        COMPILE_FLAGS -DORTHANC_ENABLE_LOGGING_PLUGIN=1
)

target_link_libraries(OrthancMongoFramework ${MONGODB_LIBS})

add_library(OrthancMongoDBIndex SHARED
        ${INDEX_RESOURCES}
        Plugins/IndexPlugin.cpp
)

add_library(OrthancMongoDBStorage SHARED
        ${STORAGE_RESOURCES}
        Plugins/StoragePlugin.cpp
)


target_link_libraries(OrthancMongoDBIndex OrthancMongoFramework)
target_link_libraries(OrthancMongoDBStorage OrthancMongoFramework)
message("Setting the version of the libraries to ${ORTHANC_PLUGIN_VERSION}")

add_definitions(
        -DORTHANC_PLUGIN_VERSION="${ORTHANC_PLUGIN_VERSION}"
)

set_target_properties(OrthancMongoDBStorage PROPERTIES
        VERSION ${ORTHANC_PLUGIN_VERSION}
        SOVERSION ${ORTHANC_PLUGIN_VERSION}
        COMPILE_FLAGS -DORTHANC_ENABLE_LOGGING_PLUGIN=1
)

set_target_properties(OrthancMongoDBIndex PROPERTIES
        VERSION ${ORTHANC_PLUGIN_VERSION}
        SOVERSION ${ORTHANC_PLUGIN_VERSION}
        COMPILE_FLAGS -DORTHANC_ENABLE_LOGGING_PLUGIN=1
)

install(
        TARGETS OrthancMongoDBIndex  OrthancMongoDBStorage
        RUNTIME DESTINATION lib    # Destination for Windows
        LIBRARY DESTINATION share/orthanc/plugins    # Destination for Linux
)

# investigate unit tests
IF (BUILD_TESTS)
    add_executable(StorageTest 
        Tests/StorageTest.cpp
        Plugins/MongoDBStorageArea.cpp
        ${DATABASES_SOURCES} 
        ${GOOGLE_TEST_SOURCES}
    )

    target_link_libraries(StorageTest ${GOOGLE_TEST_LIBRARIES})
    set_target_properties(StorageTest PROPERTIES
        COMPILE_FLAGS -DORTHANC_ENABLE_LOGGING_PLUGIN=0
    )
ENDIF()
