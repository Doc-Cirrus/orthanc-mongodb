

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

add_library(FrameworkForPlugins STATIC
        ${DATABASES_SOURCES}
        ${ORTHANC_DATABASES_ROOT}/Framework/Plugins/PluginInitialization.cpp
        Plugins/MongoDBIndex.cpp
        Plugins/MongoDBStorageArea.cpp
)

set_target_properties(FrameworkForPlugins PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        COMPILE_FLAGS -DORTHANC_ENABLE_LOGGING_PLUGIN=1
)

target_link_libraries(FrameworkForPlugins ${MONGODB_LIBS})

add_library(OrthancMongoDBIndex SHARED
        ${INDEX_RESOURCES}
        Plugins/IndexPlugin.cpp
)

add_library(OrthancMongoDBStorage SHARED
        ${STORAGE_RESOURCES}
        Plugins/StoragePlugin.cpp
)


target_link_libraries(OrthancMongoDBIndex FrameworkForPlugins)
target_link_libraries(OrthancMongoDBStorage FrameworkForPlugins)
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
    add_executable(UnitTests
            Plugins/MongoDBIndex.cpp
            UnitTests/UnitTestsMain.cpp
            ${DATABASES_SOURCES}
            ${GOOGLE_TEST_SOURCES}
    )

     target_link_libraries(UnitTests ${GOOGLE_TEST_LIBRARIES})
     set_target_properties(UnitTests PROPERTIES
            COMPILE_FLAGS -DORTHANC_ENABLE_LOGGING_PLUGIN=0
    )
ENDIF()
