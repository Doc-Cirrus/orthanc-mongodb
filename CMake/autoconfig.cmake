# Common variables
set(BUILD_DIR_POSTFIX "-build")
set(INSTALL_DIR_POSTFIX "-install")
set(CONFIGURATION_DIR_POSTFIX "-config")

# Mongo C Driver variables
set(MONGO_C_PROJECT     "mongo-c-driver")
set(MONGO_C_VERSION     "1.16.2")
set(MONGO_C_SOURCE_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}")
set(MONGO_C_BINARY_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${BUILD_DIR_POSTFIX}")
set(MONGO_C_INSTALL_DIR "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${INSTALL_DIR_POSTFIX}")
set(MONGO_C_CONFIG_DIR  "${CMAKE_BINARY_DIR}/${MONGO_C_PROJECT}${CONFIGURATION_DIR_POSTFIX}")

# Mongo CXX Driver variables
set(MONGO_CXX_PROJECT     "mongo-cxx-driver")
set(MONGO_CXX_VERSION     "3.5.0")
set(MONGO_CXX_SOURCE_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}")
set(MONGO_CXX_BINARY_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${BUILD_DIR_POSTFIX}")
set(MONGO_CXX_INSTALL_DIR "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${INSTALL_DIR_POSTFIX}")
set(MONGO_CXX_CONFIG_DIR  "${CMAKE_BINARY_DIR}/${MONGO_CXX_PROJECT}${CONFIGURATION_DIR_POSTFIX}")

# Macroses
macro(CheckError RESULT)
    if(${RESULT})
        message(FATAL_ERROR "Failed to build project: ${result}")
    endif()
endmacro()

macro(InstallPackage PROJECT_WORKING_DIR)
    execute_process(
        COMMAND ${CMAKE_COMMAND} .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${PROJECT_WORKING_DIR}
    )

    CheckError(result)

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${PROJECT_WORKING_DIR}
    )

    CheckError(result)
endmacro()

# Install mongo-c-driver

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
get_target_property(BSON_LIBS mongo::bson_shared LOCATION)
get_target_property(BSON_INCLUDE_DIRS mongo::bson_shared INTERFACE_INCLUDE_DIRECTORIES)

find_package(mongoc-1.0
            PATHS
            # Alternatives
            "${MONGO_C_INSTALL_DIR}/lib32/cmake/mongoc-1.0/"
            "${MONGO_C_INSTALL_DIR}/lib64/cmake/mongoc-1.0/"
            NO_DEFAULT_PATH
            REQUIRED)
get_target_property(MONGOC_LIBS mongo::mongoc_shared LOCATION)
get_target_property(MONGOCLIB_INCLUDE_DIRS mongo::mongoc_shared INTERFACE_INCLUDE_DIRECTORIES)

# Install mongo-cxx-driver

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/${MONGO_CXX_PROJECT}.txt.in
    ${MONGO_CXX_CONFIG_DIR}/CMakeLists.txt)

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
    get_target_property(BSONCXX_LIBS mongo::bsoncxx_static LOCATION)
    get_target_property(BSONCXX_INCLUDE_DIRS mongo::bsoncxx_static INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(MONGOCXX_LIBS mongo::mongocxx_static LOCATION)
    get_target_property(MONGOCXX_INCLUDE_DIRS mongo::mongocxx_static INTERFACE_INCLUDE_DIRECTORIES)
ELSE ()
    get_target_property(BSONCXX_LIBS mongo::bsoncxx_shared LOCATION)
    get_target_property(BSONCXX_INCLUDE_DIRS mongo::bsoncxx_shared INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(MONGOCXX_LIBS mongo::mongocxx_shared LOCATION)
    get_target_property(MONGOCXX_INCLUDE_DIRS mongo::mongocxx_shared INTERFACE_INCLUDE_DIRECTORIES)
ENDIF ()

# Set runtime path for libraries
get_filename_component(MONGO_C_RPATH "${MONGOC_LIBS}" PATH)
get_filename_component(MONGO_CXX_RPATH "${MONGOCXX_LIBS}" PATH)

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${MONGO_C_RPATH};${MONGO_CXX_RPATH}")
