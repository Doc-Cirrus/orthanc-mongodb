cmake_minimum_required(VERSION 3.2)

project(OrthancMongoDB)

set(ORTHANC_ROOT "${PROJECT_SOURCE_DIR}/../Orthanc-1.2.0" CACHE STRING "Orthanc server sources root.")
message("-- Orthanc Root: ${ORTHANC_ROOT}")

IF ( MSVC )
    set(LIBJSON_ROOT "/jsoncpp" CACHE STRING "JSONCPP root.")
    set(MONGOC_ROOT "/mongo-c-driver" CACHE STRING "Mongo C driver root.")
    set(MONGOCXX_ROOT "/mongo-cxx-driver" CACHE STRING "Mongo CXX driver root.")
    include_directories ("${LIBJSON_ROOT}/include")
    include_directories ("${MONGOC_ROOT}/include/libbson-1.0")
    include_directories ("${MONGOC_ROOT}/include/libmongoc-1.0")
    include_directories ("${MONGOCXX_ROOT}/include/bsoncxx/v_noabi")
    include_directories ("${MONGOCXX_ROOT}/include/mongocxx/v_noabi")
    set(LIBS ${LIBS} RpcRT4.Lib)
ELSE ()
    #rely on pkg-config
    include(FindPkgConfig)
    pkg_search_module(BSONCXX REQUIRED libbsoncxx)
    pkg_search_module(MONGOCXX REQUIRED libmongocxx)
    include_directories(${BSONCXX_INCLUDE_DIRS})
    include_directories(${MONGOCXX_INCLUDE_DIRS})
    set(CMAKE_CXX_FLAGS "-std=c++11")
    #Linux specific switches
    IF (NOT APPLE)
      set (CMAKE_SHARED_LINKER_FLAGS "-Wl,-z,defs")
      set(LIBS ${LIBS} "uuid" "mongoc-1.0" "bson-1.0" "bsoncxx" "mongocxx")
    ENDIF()
ENDIF ()


##################################################################
#link against static libraries
set(Boost_USE_STATIC_LIBS ON)
set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})

find_package(Boost REQUIRED COMPONENTS system)
set(LIBS ${LIBS} ${Boost_LIBRARIES})

find_library(LIBJSON_LIBS jsoncpp
      PATHS "${LIBJSON_ROOT}/lib"
)
find_library(BSON_LIBS bson-1.0
      PATHS "${MONGOC_ROOT}/lib"
)
find_library(MONGOC_LIBS mongoc-1.0
      PATHS "${MONGOC_ROOT}/lib"
)
find_library(BSONXX_LIBS bsoncxx
      PATHS "${MONGOCXX_ROOT}/lib"
)
find_library(AMONGOCXX_LIBS mongocxx
      PATHS "${MONGOCXX_ROOT}/lib"
)

IF (APPLE)
  set (MONGOC_LIBS "-Wl,-all_load ${MONGOC_LIBS}")
ENDIF()

set(LIBS ${LIBS} ${LIBJSON_LIBS} ${BSON_LIBS} ${MONGOC_LIBS} ${BSONXX_LIBS} ${AMONGOCXX_LIBS})
message("-- Libraries: ${LIBS}")

link_libraries(${LIBS})

##################################################################
include_directories("${Boost_INCLUDE_DIRS}")
include_directories ("${PROJECT_SOURCE_DIR}/Core")
include_directories ("${PROJECT_SOURCE_DIR}/IndexPlugin")
include_directories ("${PROJECT_SOURCE_DIR}/StoragePlugin")
include_directories ("${ORTHANC_ROOT}/Plugins/Include")

link_directories("${Boost_LIBRARY_DIRS}")
##################################################################

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
)

##################################################################
add_library(OrthancMongoDBIndex SHARED ${LOGGING} ${CORE_SOURCES} ${INDEX_PLUGIN_SOURCES})
add_library(OrthancMongoDBStorage SHARED ${LOGGING} ${CORE_SOURCES} ${STORAGE_PLUGIN_SOURCES})

IF (APPLE)
      set(APPLE_LIBS "sasl2" "-framework Security" "-framework CoreFoundation")
      target_link_libraries(OrthancMongoDBIndex ${APPLE_LIBS})
      target_link_libraries(OrthancMongoDBStorage ${APPLE_LIBS})
ENDIF ()
