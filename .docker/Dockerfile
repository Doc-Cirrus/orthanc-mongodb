# MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
# Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
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


# Build context: the repository root, e.g. "docker build -f .docker/Dockerfile ."
# (the compose files of this folder do the same).
#
# Stages:
#   base          - toolchain and system libraries needed to build the plugin
#   dev           - interactive development image (sources are bind-mounted by docker-compose.override.dev.yaml.example)
#   build         - compiles the plugins and the unit tests from the sources in the build context
#   runtime       - Orthanc (Linux Standard Base binaries) with the MongoDB plugins enabled

# Versions of the Linux Standard Base downloads used by the runtime stage:
# https://orthanc.uclouvain.be/downloads/linux-standard-base/index.html
ARG ORTHANC_VERSION=1.13.0
ARG ORTHANC_EXPLORER_2_VERSION=1.15.0
ARG STONE_WEB_VIEWER_VERSION=3.0
ARG ORTHANC_DICOMWEB_VERSION=1.24


FROM oraclelinux:9 AS base

RUN dnf config-manager --enable ol9_addons
RUN dnf -y install patch \
 git \
 curl \
 libuuid-devel \
 openssl-devel \
 cyrus-sasl-devel \
 zlib-devel \
 unzip \
 cmake \
 make \
 gcc \
 gdb \
 gcc-c++ \
 python3 \
 && dnf clean all


FROM base AS dev

WORKDIR /usr/local/src


FROM base AS build

ARG BUILD_TYPE=Release

WORKDIR /usr/share/src
COPY . /usr/share/src

RUN cmake -S /usr/share/src/MongoDB -B /usr/share/build \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DSTATIC_BUILD=ON \
      -DALLOW_DOWNLOADS=ON \
      -DAUTO_INSTALL_DEPENDENCIES=ON \
      -DBUILD_TESTS=ON \
 && cmake --build /usr/share/build -j"$(nproc)" \
 && cmake --install /usr/share/build


FROM base AS runtime

# Re-declared so that the global values are visible inside this stage
ARG ORTHANC_VERSION
ARG ORTHANC_EXPLORER_2_VERSION
ARG STONE_WEB_VIEWER_VERSION
ARG ORTHANC_DICOMWEB_VERSION
ARG LSB_DOWNLOADS=https://orthanc.uclouvain.be/downloads/linux-standard-base

ENV MONGO_URL=mongodb://database:27017/inpacs

WORKDIR /usr/local/runtime

RUN curl -fL ${LSB_DOWNLOADS}/orthanc/${ORTHANC_VERSION}/Orthanc -o Orthanc
RUN curl -fL ${LSB_DOWNLOADS}/orthanc/${ORTHANC_VERSION}/libServeFolders.so -o libServeFolders.so
RUN curl -fL ${LSB_DOWNLOADS}/orthanc/${ORTHANC_VERSION}/libModalityWorklists.so -o libModalityWorklists.so
RUN curl -fL ${LSB_DOWNLOADS}/orthanc-explorer-2/${ORTHANC_EXPLORER_2_VERSION}/libOrthancExplorer2.so -o libOrthancExplorer2.so
RUN curl -fL ${LSB_DOWNLOADS}/orthanc-explorer-2/${ORTHANC_EXPLORER_2_VERSION}/dist.zip -o dist.zip
RUN curl -fL ${LSB_DOWNLOADS}/stone-web-viewer/${STONE_WEB_VIEWER_VERSION}/libStoneWebViewer.so -o libStoneWebViewer.so
RUN curl -fL ${LSB_DOWNLOADS}/stone-web-viewer/${STONE_WEB_VIEWER_VERSION}/wasm-binaries.zip -o wasm-binaries.zip
RUN curl -fL ${LSB_DOWNLOADS}/orthanc-dicomweb/${ORTHANC_DICOMWEB_VERSION}/libOrthancDicomWeb.so -o libOrthancDicomWeb.so

RUN chmod +x ./Orthanc
RUN unzip dist.zip
RUN unzip wasm-binaries.zip

COPY --from=build /usr/local/share/orthanc/plugins/ /usr/local/runtime/
COPY --from=build /usr/share/src/Resources/Config/configuration.json /usr/local/runtime/configuration.json

EXPOSE 4242
EXPOSE 8042

CMD ["./Orthanc", "./configuration.json"]
