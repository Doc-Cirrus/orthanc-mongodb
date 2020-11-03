cmake_minimum_required(VERSION 3.2)

project(OrthancMongoDB CXX)

set(ORTHANC_ROOT "${PROJECT_SOURCE_DIR}/../Orthanc-1.8.0" CACHE STRING "Orthanc server sources root.")
set(BUILD_TESTS OFF CACHE BOOL "Option to build tests.")
set(BUILD_WITH_GCOV OFF CACHE BOOL "Option to build with gcov for the code coverage.")
set(LINK_STATIC_LIBS OFF CACHE BOOL "Option to link against static mongo-c-driver and mongo-cxx-driver.")
set(AUTO_INSTALL_DEPENDENCIES OFF CACHE BOOL "Option to install some required libraries automatically. Libraries wiill be installed in project build folder.")

set(ORTHANC_OPTIMAL_VERSION_MAJOR    1)
set(ORTHANC_OPTIMAL_VERSION_MINOR    8)
set(ORTHANC_OPTIMAL_VERSION_REVISION 0)

message("-- Orthanc Root: ${ORTHANC_ROOT}")

IF ( MSVC )
    IF (NOT AUTO_INSTALL_DEPENDENCIES)
      set(LIBJSON_ROOT "/jsoncpp" CACHE STRING "JSONCPP root.")
      set(MONGOC_ROOT "/mongo-c-driver" CACHE STRING "Mongo C driver root.")
      set(MONGOCXX_ROOT "/mongo-cxx-driver" CACHE STRING "Mongo CXX driver root.")
      # Set reqired include pathes for MSVC
      set(BSON_INCLUDE_DIRS      "${MONGOC_ROOT}/include/libbson-1.0")
      set(MONGOCLIB_INCLUDE_DIRS "${MONGOC_ROOT}/include/libmongoc-1.0")
      set(BSONCXX_INCLUDE_DIRS   "${MONGOCXX_ROOT}/include/bsoncxx/v_noabi")
      set(MONGOCXX_INCLUDE_DIRS  "${MONGOCXX_ROOT}/include/mongocxx/v_noabi")
      set(JSONCPP_INCLUDE_DIRS   "${LIBJSON_ROOT}/include")
    ELSE ()
      # Install, build and configure next components: jsoncpp, libbson, libmongoc, libbsoncxx, libmongocxx
      include(CMake/autoconfig.cmake)
    ENDIF ()
    set(LIBS ${LIBS} RpcRT4.Lib)
ELSE ()
    #rely on pkg-config
    include(FindPkgConfig)
    IF (NOT AUTO_INSTALL_DEPENDENCIES)
      find_package(libmongoc-1.0 REQUIRED)
      pkg_search_module(JSONCPP REQUIRED jsoncpp)
      IF (LINK_STATIC_LIBS)
	      find_package(libbsoncxx-static REQUIRED)
	      find_package(libmongocxx-static REQUIRED)
      ELSE()
        pkg_search_module(BSONCXX REQUIRED libbsoncxx)
        pkg_search_module(MONGOCXX REQUIRED libmongocxx)
      ENDIF()
    ELSE ()
      # Install, build and configure next components: jsoncpp, libbson, libmongoc, libbsoncxx, libmongocxx
      include(CMake/autoconfig.cmake)
    ENDIF ()
    pkg_search_module(OPENSSL openssl)
    IF (OPENSSL_FOUND)
      find_library(CYRUS sasl2)
      set(LIBS ${LIBS} ${OPENSSL_LIBRARIES} "sasl2")
    ENDIF()
    IF (CYRUS_FOUND)
      set(LIBS ${LIBS} ${CYRUS_LIBRARIES})
    ENDIF()
    set(CMAKE_CXX_FLAGS "-std=c++14")
    #Linux specific switches
    IF (NOT APPLE)
      set (CMAKE_SHARED_LINKER_FLAGS "-Wl,-z,defs")
      set(LIBS ${LIBS} "uuid" "pthread" "rt")
    ENDIF()
    set(LIBS ${LIBS} "z" "resolv")
ENDIF ()

# Include directories
include_directories(${BSON_INCLUDE_DIRS})
include_directories(${MONGOC_INCLUDE_DIRS})
include_directories(${LIBMONGOCXX_STATIC_INCLUDE_DIRS})
include_directories(${LIBBSONCXX_STATIC_INCLUDE_DIRS})
include_directories(${JSONCPP_INCLUDE_DIRS})

IF (NOT AUTO_INSTALL_DEPENDENCIES)
  set(LIBJSON_LIB_NAMES  jsoncpp   )
  set(BSON_LIB_NAMES     bson-1.0  )
  set(MONGOC_LIB_NAMES   mongoc-1.0)
  set(BSONCXX_LIB_NAMES  bsoncxx   )
  set(MONGOCXX_LIB_NAMES mongocxx  )

  IF (LINK_STATIC_LIBS)
      ##################################################################
      #link against static libraries
      set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
      set(LIBJSON_LIB_NAMES   jsoncpp_static                ${LIBJSON_LIB_NAMES} )
      set(BSON_LIB_NAMES      bson-static-1.0 bson-1.0      ${BSON_LIB_NAMES}    )
      set(MONGOC_LIB_NAMES    mongoc-static-1.0 mongoc-1.0  ${MONGOC_LIB_NAMES}  )
      set(BSONCXX_LIB_NAMES   bsoncxx-static                ${LIBBSONCXX_STATIC_LIBRARIES} )
      set(MONGOCXX_LIB_NAMES  mongocxx-static               ${LIBMONGOCXX_STATIC_LIBRARIES})
  ENDIF()

  find_library(LIBJSON_LIBS
      NAMES ${LIBJSON_LIB_NAMES}
      PATHS "${LIBJSON_ROOT}/lib"
  )
  find_library(BSON_LIBS
      NAMES ${BSON_LIB_NAMES}
      PATHS "${MONGOC_ROOT}/lib"
  )
  find_library(MONGOC_LIBS
      NAMES ${MONGOC_LIB_NAMES}
      PATHS "${MONGOC_ROOT}/lib"
  )
  find_library(BSONXX_LIBS
      NAMES ${BSONCXX_LIB_NAMES}
      PATHS "${MONGOCXX_ROOT}/lib"
  )
  find_library(AMONGOCXX_LIBS
      NAMES ${MONGOCXX_LIB_NAMES}
      PATHS "${MONGOCXX_ROOT}/lib"
  )
ENDIF ()

message("Found libraries:")
message("    LIBJSON_LIBS   " ${LIBJSON_LIBS} )
message("    MONGOCXX_LIBS  " ${AMONGOCXX_LIBS} )
message("    BSONCXX_LIBS   " ${BSONXX_LIBS} )
message("    MONGOC_LIBS    " ${MONGOC_LIBS} )
message("    BSON_LIBS      " ${BSON_LIBS})

set(LIBS ${LIBS} ${LIBJSON_LIBS} ${AMONGOCXX_LIBS} ${BSONXX_LIBS} ${MONGOC_LIBS} ${BSON_LIBS})
link_libraries(${LIBS})

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


target_link_libraries(OrthancMongoDBIndex ${LIBS})
target_link_libraries(OrthancMongoDBStorage ${LIBS})

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

  IF (UNIX)
      pkg_search_module(GTEST REQUIRED gtest)
      include_directories(${GTEST_INCLUDE_DIRS})
  ENDIF()

  include_directories ("${PROJECT_SOURCE_DIR}/Tests")
  find_library(GOOGLE_TEST_LIB gtest)

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
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lpthread -lm")
  ENDIF()

  add_executable(IndexTest ${CORE_SOURCES} ${INDEX_PLUGIN_SOURCES} ${TEST_SOURCES_INDEX})
  add_executable(StorageTest ${CORE_SOURCES} ${STORAGE_PLUGIN_SOURCES} ${TEST_SOURCES_STORAGE})
  add_executable(ConfigurationTest ${CORE_SOURCES} ${TEST_SOURCES_CONFIGURATION})
  target_link_libraries(IndexTest ${APPLE_LIBS} ${LIBS} ${GOOGLE_TEST_LIB})
  target_link_libraries(StorageTest ${APPLE_LIBS} ${LIBS} ${GOOGLE_TEST_LIB})
  target_link_libraries(ConfigurationTest ${APPLE_LIBS} ${LIBS} ${GOOGLE_TEST_LIB})
ENDIF()
