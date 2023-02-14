# MongoDB configuration cmake deps

IF ( MSVC )
    IF (NOT AUTO_INSTALL_DEPENDENCIES)
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
      include(${CMAKE_CURRENT_LIST_DIR}/../MongoDB/AutoConfig.cmake)
    ENDIF ()
    set(MONGODB_LIBS ${MONGODB_LIBS} RpcRT4.Lib)
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
      include(${CMAKE_CURRENT_LIST_DIR}/../MongoDB/AutoConfig.cmake)
    ENDIF ()
    pkg_search_module(OPENSSL openssl)
    IF (OPENSSL_FOUND)
      find_library(CYRUS sasl2)
      set(MONGODB_LIBS ${MONGODB_LIBS} ${OPENSSL_LIBRARIES} "sasl2")
    ENDIF()
    IF (CYRUS_FOUND)
      set(MONGODB_LIBS ${MONGODB_LIBS} ${CYRUS_LIBRARIES})
    ENDIF()
    set(CMAKE_CXX_FLAGS "-std=c++14")
    #Linux specific switches
    IF (NOT APPLE)
      set (CMAKE_SHARED_LINKER_FLAGS "-Wl,-z,defs")
      set(MONGODB_LIBS ${MONGODB_LIBS} "uuid" "pthread" "rt")
    ENDIF()
    set(MONGODB_LIBS ${MONGODB_LIBS} "z" "resolv")
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

set(MONGODB_LIBS ${MONGODB_LIBS} ${LIBJSON_LIBS} ${AMONGOCXX_LIBS} ${BSONXX_LIBS} ${MONGOC_LIBS} ${BSON_LIBS})
link_libraries(${MONGODB_LIBS})
