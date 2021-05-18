#!/usr/bin/env sh

apt-get update -y && \
apt-get upgrade -y && \
apt-get dist-upgrade -y && \
apt-get install build-essential software-properties-common -y && \
add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
apt-get update -y && \
apt-get install gcc-9 g++-9 -y && \
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 60 --slave /usr/bin/g++ g++ /usr/bin/g++-9 && \
update-alternatives --config gcc

select gcc-9