# Common variables
set(BUILD_DIR_POSTFIX "-build")
set(INSTALL_DIR_POSTFIX "-install")
set(CONFIGURATION_DIR_POSTFIX "-config")

# JsonCpp variables
set(JSONCPP_PROJECT     "jsoncpp")
set(JSONCPP_VERSION     "1.9.4")
set(JSONCPP_SOURCE_DIR  "${CMAKE_BINARY_DIR}/${JSONCPP_PROJECT}")
set(JSONCPP_BINARY_DIR  "${CMAKE_BINARY_DIR}/${JSONCPP_PROJECT}${BUILD_DIR_POSTFIX}")
set(JSONCPP_INSTALL_DIR "${CMAKE_BINARY_DIR}/${JSONCPP_PROJECT}${INSTALL_DIR_POSTFIX}")
set(JSONCPP_CONFIG_DIR  "${CMAKE_BINARY_DIR}/${JSONCPP_PROJECT}${CONFIGURATION_DIR_POSTFIX}")

# Mongo C Driver variables
set(MONGO_C_PROJECT     "mongo-c-driver")
set(MONGO_C_VERSION     "1.17.1")
set(MONGO_C_SOURCE_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}")
set(MONGO_C_BINARY_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${BUILD_DIR_POSTFIX}")
set(MONGO_C_INSTALL_DIR "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${INSTALL_DIR_POSTFIX}")
set(MONGO_C_CONFIG_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${CONFIGURATION_DIR_POSTFIX}")

# Mongo CXX Driver variables
set(MONGO_CXX_PROJECT     "mongo-cxx-driver")
set(MONGO_CXX_VERSION     "3.6.0")
set(MONGO_CXX_SOURCE_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}")
set(MONGO_CXX_BINARY_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${BUILD_DIR_POSTFIX}")
set(MONGO_CXX_INSTALL_DIR "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${INSTALL_DIR_POSTFIX}")
set(MONGO_CXX_CONFIG_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${CONFIGURATION_DIR_POSTFIX}")

string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_UPPERCASE)

# Macroses
macro(CheckError RESULT)
    if(${RESULT})
        message(FATAL_ERROR "Failed to build project: ${RESULT}")
    endif()
endmacro()

macro(InstallPackage PROJECT_WORKING_DIR)
    execute_process(
        COMMAND ${CMAKE_COMMAND} .
        RESULT_VARIABLE RESULT
        WORKING_DIRECTORY ${PROJECT_WORKING_DIR}
    )

    CheckError(RESULT)

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE}
        RESULT_VARIABLE RESULT
        WORKING_DIRECTORY ${PROJECT_WORKING_DIR}
    )

    CheckError(RESULT)
endmacro()

IF (MSVC AND LINK_STATIC_LIBS)
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

# Install JsonCpp

IF (LINK_STATIC_LIBS)
    set(JSONCPP_LINK_SHARED_LIBS OFF)
ELSE ()
    set(JSONCPP_LINK_SHARED_LIBS ON)
ENDIF ()

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/${JSONCPP_PROJECT}.txt.in
    ${JSONCPP_CONFIG_DIR}/CMakeLists.txt)

unset(JSONCPP_LINK_SHARED_LIBS)

InstallPackage(${JSONCPP_CONFIG_DIR})

set(jsoncpp_DIR "${JSONCPP_INSTALL_DIR}/lib/cmake/jsoncpp/")

find_package(jsoncpp
            PATHS
            # Alternatives
            "${JSONCPP_INSTALL_DIR}/lib32/cmake/jsoncpp/"
            "${JSONCPP_INSTALL_DIR}/lib64/cmake/jsoncpp/"
            NO_DEFAULT_PATH
            REQUIRED)

IF (LINK_STATIC_LIBS)
    get_target_property(LIBJSON_LIBS jsoncpp_lib_static LOCATION)
    get_target_property(JSONCPP_INCLUDE_DIRS jsoncpp_lib_static INTERFACE_INCLUDE_DIRECTORIES)
ELSE ()
    IF (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        get_target_property(LIBJSON_LIBS jsoncpp_lib IMPORTED_IMPLIB_${CMAKE_BUILD_TYPE_UPPERCASE})
    ELSE ()
        get_target_property(LIBJSON_LIBS jsoncpp_lib LOCATION)
    ENDIF ()
    get_target_property(JSONCPP_INCLUDE_DIRS jsoncpp_lib INTERFACE_INCLUDE_DIRECTORIES)
ENDIF ()

# Install mongo-c-driver

STRING(REGEX MATCH "-fPIC" FPIC ${CMAKE_CXX_FLAGS})
IF(${FPIC} MATCHES "-fPIC")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
    set(CMAKE_C_FLAGS_${CMAKE_BUILD_TYPE_UPPERCASE} "${CMAKE_C_FLAGS_${CMAKE_BUILD_TYPE_UPPERCASE}} -fPIC")
ENDIF()

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/${MONGO_C_PROJECT}.txt.in
    ${MONGO_C_CONFIG_DIR}/CMakeLists.txt)

InstallPackage(${MONGO_C_CONFIG_DIR})

set(bson-1.0_DIR "${MONGO_C_INSTALL_DIR}/lib/cmake/bson-1.0/")
set(mongoc-1.0_DIR "${MONGO_C_INSTALL_DIR}/lib/cmake/mongoc-1.0/")

find_package(bson-1.0
            PATHS
            # Alternatives
            "${MONGO_C_INSTALL_DIR}/lib32/cmake/bson-1.0/"
            "${MONGO_C_INSTALL_DIR}/lib64/cmake/bson-1.0/"
            NO_DEFAULT_PATH
            REQUIRED)

find_package(mongoc-1.0
            PATHS
            # Alternatives
            "${MONGO_C_INSTALL_DIR}/lib32/cmake/mongoc-1.0/"
            "${MONGO_C_INSTALL_DIR}/lib64/cmake/mongoc-1.0/"
            NO_DEFAULT_PATH
            REQUIRED)

IF (LINK_STATIC_LIBS)
    get_target_property(BSON_LIBS mongo::bson_static LOCATION)
    get_target_property(BSON_INCLUDE_DIRS mongo::bson_static INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(MONGOC_LIBS mongo::mongoc_static LOCATION)
    get_target_property(MONGOCLIB_INCLUDE_DIRS mongo::mongoc_static INTERFACE_INCLUDE_DIRECTORIES)
ELSE ()
    IF (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        get_target_property(BSON_LIBS mongo::bson_shared IMPORTED_IMPLIB_${CMAKE_BUILD_TYPE_UPPERCASE})
    ELSE ()
        get_target_property(BSON_LIBS mongo::bson_shared LOCATION)
    ENDIF ()
    get_target_property(BSON_INCLUDE_DIRS mongo::bson_shared INTERFACE_INCLUDE_DIRECTORIES)

    IF (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        get_target_property(MONGOC_LIBS mongo::mongoc_shared IMPORTED_IMPLIB_${CMAKE_BUILD_TYPE_UPPERCASE})
    ELSE ()
        get_target_property(MONGOC_LIBS mongo::mongoc_shared LOCATION)
    ENDIF ()
    get_target_property(MONGOCLIB_INCLUDE_DIRS mongo::mongoc_shared INTERFACE_INCLUDE_DIRECTORIES)
ENDIF ()

#Install mongo-cxx-driver

IF (LINK_STATIC_LIBS)
    set(MONGO_CXX_BUILD_SHARED_LIBS OFF)
ELSE ()
    set(MONGO_CXX_BUILD_SHARED_LIBS ON)
ENDIF ()

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/${MONGO_CXX_PROJECT}.txt.in
    ${MONGO_CXX_CONFIG_DIR}/CMakeLists.txt)

unset(MONGO_CXX_BUILD_SHARED_LIBS)

InstallPackage(${MONGO_CXX_CONFIG_DIR})

set(bsoncxx_DIR "${MONGO_CXX_INSTALL_DIR}/lib/cmake/bsoncxx-${MONGO_CXX_VERSION}/")
set(mongocxx_DIR "${MONGO_CXX_INSTALL_DIR}/lib/cmake/mongocxx-${MONGO_CXX_VERSION}/")

find_package(bsoncxx
            PATHS
            # Alternatives
            "${MONGO_CXX_INSTALL_DIR}/lib32/cmake/bsoncxx-${MONGO_CXX_VERSION}/"
            "${MONGO_CXX_INSTALL_DIR}/lib64/cmake/bsoncxx-${MONGO_CXX_VERSION}/"
            NO_DEFAULT_PATH
            REQUIRED)

find_package(mongocxx
            PATHS
            # Alternatives
            "${MONGO_CXX_INSTALL_DIR}/lib32/cmake/mongocxx-${MONGO_CXX_VERSION}/"
            "${MONGO_CXX_INSTALL_DIR}/lib64/cmake/mongocxx-${MONGO_CXX_VERSION}/"
            NO_DEFAULT_PATH
            REQUIRED)

IF (LINK_STATIC_LIBS)
    get_target_property(BSONXX_LIBS mongo::bsoncxx_static LOCATION)
    get_target_property(BSONCXX_INCLUDE_DIRS mongo::bsoncxx_static INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(AMONGOCXX_LIBS mongo::mongocxx_static LOCATION)
    get_target_property(MONGOCXX_INCLUDE_DIRS mongo::mongocxx_static INTERFACE_INCLUDE_DIRECTORIES)
ELSE ()
    IF (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        get_target_property(BSONXX_LIBS mongo::bsoncxx_shared IMPORTED_IMPLIB_${CMAKE_BUILD_TYPE_UPPERCASE})
    ELSE ()
        get_target_property(BSONXX_LIBS mongo::bsoncxx_shared LOCATION)
    ENDIF ()
    get_target_property(BSONCXX_INCLUDE_DIRS mongo::bsoncxx_shared INTERFACE_INCLUDE_DIRECTORIES)

    IF (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        get_target_property(AMONGOCXX_LIBS mongo::mongocxx_shared IMPORTED_IMPLIB_${CMAKE_BUILD_TYPE_UPPERCASE})
    ELSE ()
        get_target_property(AMONGOCXX_LIBS mongo::mongocxx_shared LOCATION)
    ENDIF ()
    get_target_property(MONGOCXX_INCLUDE_DIRS mongo::mongocxx_shared INTERFACE_INCLUDE_DIRECTORIES)
ENDIF ()

# Set runtime path for libraries
get_filename_component(MONGO_C_RPATH "${MONGOC_LIBS}" PATH)
get_filename_component(MONGO_CXX_RPATH "${AMONGOCXX_LIBS}" PATH)
get_filename_component(JSONCPP_RPATH "${LIBJSON_LIBS}" PATH)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${MONGO_C_RPATH};${MONGO_CXX_RPATH};${JSONCPP_RPATH}")

# Inlude boost headers in case if boost used
IF (BOOST_ROOT)
    include_directories(${BOOST_ROOT})
ENDIF ()

IF (MSVC AND LINK_STATIC_LIBS)
    # Link with some system libraries
    set(LIBS ${LIBS} ws2_32.lib Secur32.lib Crypt32.lib BCrypt.lib Dnsapi.lib)
    # Add preprocessor definitions which are required for correct linking
    add_definitions(
      -DBSON_STATIC
      -DMONGOC_STATIC
      -DBSONCXX_STATIC
      -DMONGOCXX_STATIC
    )
ENDIF ()
