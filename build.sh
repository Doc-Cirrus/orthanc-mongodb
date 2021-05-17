#!/bin/bash
if [ $# -eq "0" ]
then
    echo "No arguments supplied.\n"
    echo "1 - Orthanc Version\n"
    echo "2 - Build Type (Debug, Release)"
fi

PLUGIN_DIR=$(pwd)

. /etc/os-release
OS=$NAME
if [[ $OS == "Ubuntu" ]] 
then
    apt update -y
    apt -y install build-essential unzip cmake python make libsasl2-dev uuid-dev libssl-dev zlib1g-dev git curl wget
    CMAKE_EXE=cmake
elif [[ $OS == "CentOS Linux" ]]
then
   echo "Centos system"
    yum -y install centos-release-scl centos-release-scl-rh epel-release
    yum -y install make devtoolset-8 libuuid-devel python openssl-devel cyrus-sasl-devel cmake3 zlib-devel gcc-c++ wget
    CMAKE_EXE=cmake3
else
echo "Not CentOS or Ubuntu"
fi

echo $OS

mkdir orthanc_mongodb_plugin_build
pushd orthanc_mongodb_plugin_build
wget https://www.orthanc-server.com/downloads/get.php?path=/orthanc/Orthanc-$1.tar.gz
tar -zxf get.php?path=%2Forthanc%2FOrthanc-$1.tar.gz

#Build Orthanc plugin
ROOT_DIR=$(pwd)
mkdir plugin_build
pushd plugin_build
$CMAKE_EXE -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr  -DCMAKE_BUILD_TYPE=$2 -DORTHANC_ROOT=$ROOT_DIR/Orthanc-$1  -DBUILD_TESTS=ON  \-DAUTO_INSTALL_DEPENDENCIES=ON $PLUGIN_DIR
nproc --all | make -j

#Build Orthanc Service 
popd
mkdir orthanc_service_build
pushd orthanc_service_build
$CMAKE_EXE -DSTATIC_BUILD=ON -DCMAKE_BUILD_TYPE=$2 $ROOT_DIR/Orthanc-$1/OrthancServer/
nproc --all | make -j
