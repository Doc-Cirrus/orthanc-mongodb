# MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
# Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
# Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
# Department, University Hospital of Liege, Belgium
# Copyright (C) 2017-2023 Osimis S.A., Belgium
# Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
# Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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


#####################################################################
## Import the parameters of the Orthanc Framework
#####################################################################

include(${CMAKE_CURRENT_LIST_DIR}/../Orthanc/CMake/DownloadOrthancFramework.cmake)

if (NOT ORTHANC_FRAMEWORK_SOURCE STREQUAL "system")
  include(${ORTHANC_FRAMEWORK_ROOT}/../Resources/CMake/OrthancFrameworkParameters.cmake)
endif()


#####################################################################
## CMake parameters tunable by the user
#####################################################################

set(AUTO_INSTALL_DEPENDENCIES OFF CACHE BOOL "Download and build the MongoDB C and C++ drivers instead of using the system ones")
set(LINK_STATIC_LIBS OFF CACHE BOOL "Link against the static version of the system MongoDB drivers")
set(PORTABLE_BUILD OFF CACHE BOOL "Plugins that only depend on glibc: static OpenSSL (from OPENSSL_ROOT_DIR), libstdc++ and libgcc, no SASL, bundled zlib (with STATIC_BUILD and AUTO_INSTALL_DEPENDENCIES)")
set(MONGOC_ROOT "" CACHE PATH "Installation prefix of the MongoDB C driver (optional)")
set(MONGOCXX_ROOT "" CACHE PATH "Installation prefix of the MongoDB C++ driver (optional)")


#####################################################################
## Internal CMake parameters to enable the optional subcomponents of
## the database engines
#####################################################################

set(ENABLE_MONGODB_BACKEND ON)
set(ENABLE_PROTOBUF ON)
set(ENABLE_PROTOBUF_COMPILER ON)
