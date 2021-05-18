cmake_minimum_required(VERSION 3.2)

project(OrthancMongoDB CXX)

set(ORTHANC_ROOT "${PROJECT_SOURCE_DIR}/../Orthanc-1.9.1" CACHE STRING "Orthanc server sources root.")
set(BUILD_TESTS OFF CACHE BOOL "Option to build tests.")
set(BUILD_WITH_GCOV OFF CACHE BOOL "Option to build with gcov for the code coverage.")
set(LINK_STATIC_LIBS OFF CACHE BOOL "Option to link against static mongo-c-driver and mongo-cxx-driver.")
set(AUTO_INSTALL_DEPENDENCIES OFF CACHE BOOL "Option to install some required libraries automatically. Libraries wiill be installed in project build folder.")


include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(ORTHANC_OPTIMAL_VERSION_MAJOR    1)
set(ORTHANC_OPTIMAL_VERSION_MINOR    9)
set(ORTHANC_OPTIMAL_VERSION_REVISION 1)

message("-- Orthanc Root: ${ORTHANC_ROOT}")

##################################################################
include_directories ("${PROJECT_SOURCE_DIR}/Core")
include_directories ("${PROJECT_SOURCE_DIR}/IndexPlugin")
include_directories ("${PROJECT_SOURCE_DIR}/StoragePlugin")
include_directories ("${ORTHANC_ROOT}/OrthancServer/Plugins/Include")
##################################################################
add_definitions(
	-DHAS_ORTHANC_EXCEPTION=1 
	-DORTHANC_ENABLE_LOGGING=1 
	-DORTHANC_ENABLE_LOGGING_PLUGIN=1 
	-DORTHANC_ENABLE_LOGGING_STDIO=1
  -DORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT=1
	-DORTHANC_OPTIMAL_VERSION_MAJOR=${ORTHANC_OPTIMAL_VERSION_MAJOR}
	-DORTHANC_OPTIMAL_VERSION_MINOR=${ORTHANC_OPTIMAL_VERSION_MINOR}
	-DORTHANC_OPTIMAL_VERSION_REVISION=${ORTHANC_OPTIMAL_VERSION_REVISION}
)

set(CORE_SOURCES
  "Core/MongoDBConnection.cpp"
  "Core/Configuration.cpp"
)

set(INDEX_PLUGIN_SOURCES
  "IndexPlugin/MongoDBBackend.cpp"
  "IndexPlugin/Plugin.cpp"
)

set(STORAGE_PLUGIN_SOURCES
  "StoragePlugin/MongoDBStorageArea.cpp"
  "StoragePlugin/Plugin.cpp"
  "StoragePlugin/MongoDBGridFS.cpp"
)

##################################################################
add_library(OrthancMongoDBIndex SHARED ${CORE_SOURCES} ${INDEX_PLUGIN_SOURCES})
add_library(OrthancMongoDBStorage SHARED ${CORE_SOURCES} ${STORAGE_PLUGIN_SOURCES})

IF (NOT APPLE)
      set (CMAKE_SHARED_LINKER_FLAGS "-Wl,-z,defs")
      set(LIBS "uuid" "pthread" "rt" )
ENDIF()
set(LIBS ${LIBS} "z" "resolv")


target_link_libraries(OrthancMongoDBIndex ${CONAN_LIBS} ${LIBS})
target_link_libraries(OrthancMongoDBStorage ${CONAN_LIBS} ${LIBS})

IF (APPLE)
  set(APPLE_LIBS "sasl2" "-framework Security" "-framework CoreFoundation")
  target_link_libraries(OrthancMongoDBIndex ${APPLE_LIBS})
  target_link_libraries(OrthancMongoDBStorage ${APPLE_LIBS})
ENDIF ()

install(
  TARGETS OrthancMongoDBIndex OrthancMongoDBStorage
  RUNTIME DESTINATION lib    # Destination for Windows
  LIBRARY DESTINATION share/orthanc/plugins    # Destination for Linux
)

IF (BUILD_TESTS)

  IF (BUILD_WITH_GCOV)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
    IF(UNIX AND NOT APPLE)
      set(LIBS ${LIBS} gcov)
    ENDIF()
  ENDIF()

  set(TEST_SOURCES_INDEX
        "Tests/IndexTest.cpp"
  )
  set(TEST_SOURCES_STORAGE
        "Tests/StorageTest.cpp"
  )

  set(TEST_SOURCES_CONFIGURATION
        "Tests/ConfigurationTest.cpp"
  )

  IF (UNIX AND NOT APPLE)
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lpthread -lm -luuid")
  ENDIF()

  add_executable(IndexTest ${CORE_SOURCES} ${INDEX_PLUGIN_SOURCES} ${TEST_SOURCES_INDEX})
  add_executable(StorageTest ${CORE_SOURCES} ${STORAGE_PLUGIN_SOURCES} ${TEST_SOURCES_STORAGE})
  add_executable(ConfigurationTest ${CORE_SOURCES} ${TEST_SOURCES_CONFIGURATION})
  target_link_libraries(IndexTest ${APPLE_LIBS} ${CONAN_LIBS} ${LIBS} )
  target_link_libraries(StorageTest ${APPLE_LIBS} ${CONAN_LIBS} ${LIBS} )
  target_link_libraries(ConfigurationTest ${APPLE_LIBS} ${CONAN_LIBS} ${LIBS})
ENDIF()
