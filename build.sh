#!/usr/bin/env bash

if [$# -eq "0"]
then
    echo "No arguments supplied.\n"
    echo "1 - Orthanc Version\n"
    echo "2 - Build Type (Debug, Release)"
fi

PLUGIN_DIR=$(pwd)

. /etc/os-release
OS=$NAME
if [ $OS == "Ubuntu" ] 
then
    sudo apt -y install build-essential unzip cmake make libsasl2-dev uuid-dev libssl-dev zlib1g-dev git curl
fi

mkdir orthanc_mongodb_plugin_build
pushd orthanc_mongodb_plugin_build
wget https://www.orthanc-server.com/downloads/get.php?path=/orthanc/Orthanc-$1.tar.gz
tar -zxf get.php?path=%2Forthanc%2FOrthanc-$1.tar.gz

#Build Orthanc plugin
ROOT_DIR=$(pwd)
mkdir plugin_build
pushd plugin_build
cmake -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=$2 -DORTHANC_ROOT=$ROOT_DIR/Orthanc-$1  -DBUILD_TESTS=ON  \-DAUTO_INSTALL_DEPENDENCIES=ON $PLUGIN_DIR
nproc --all | make -j

#Build Orthanc Service 
popd
mkdir orthanc_service_build
pushd orthanc_service_build
cmake -DSTATIC_BUILD=ON -DCMAKE_BUILD_TYPE=$2 $ROOT_DIR/Orthanc-$1/OrthancServer/
nproc --all | make -j
