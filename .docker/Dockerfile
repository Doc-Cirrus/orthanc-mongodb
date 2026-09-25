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
#   coverage      - the unit tests, instrumented for code coverage (Resources/Coverage.sh)
#   lsb           - portable plugins, which only need glibc >= 2.28 (PORTABLE_BUILD), checked by Resources/CheckPortability.sh
#   lsb-artifacts - the files of the "lsb" stage only, for "docker build --target lsb-artifacts --output type=local,dest=<dir>"
#   runtime       - Orthanc (Linux Standard Base binaries) with the MongoDB plugins enabled
#   integration   - the orthanc-tests suite, run against the portable plugins (.docker/integration/run-orthanc-tests.sh)

# Versions of the Linux Standard Base downloads used by the runtime stage:
# https://orthanc.uclouvain.be/downloads/linux-standard-base/index.html
ARG ORTHANC_VERSION=1.13.0
ARG ORTHANC_EXPLORER_2_VERSION=1.15.0
ARG STONE_WEB_VIEWER_VERSION=3.0
ARG ORTHANC_DICOMWEB_VERSION=1.24

# Tag of the orthanc-tests suite run by the integration stage
ARG ORTHANC_TESTS_VERSION=Orthanc-1.13.0


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


FROM base AS coverage

RUN dnf -y install python3-pip && dnf clean all \
 && pip3 install --no-cache-dir gcovr==8.3

WORKDIR /usr/share/src
COPY . /usr/share/src

RUN cmake -S /usr/share/src/MongoDB -B /usr/share/build-coverage \
      -DCMAKE_BUILD_TYPE=Debug \
      -DSTATIC_BUILD=ON \
      -DALLOW_DOWNLOADS=ON \
      -DAUTO_INSTALL_DEPENDENCIES=ON \
      -DBUILD_TESTS=ON \
      -DENABLE_COVERAGE=ON \
 && cmake --build /usr/share/build-coverage --target UnitTests -j"$(nproc)"

# e.g. docker run -v "$PWD/out:/out" <image> mongodb://database:27017/ "mongodb://database-rs:27017/?replicaSet=rs0" /out
ENTRYPOINT ["/usr/share/src/Resources/Coverage.sh"]


# Portable plugins: built with glibc 2.28 (AlmaLinux 8) and a recent GCC, with every other
# dependency linked statically, so they load on any x86_64 Linux with glibc >= 2.28
FROM quay.io/pypa/manylinux_2_28_x86_64:2026.09.23-1 AS lsb

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

ARG BUILD_TYPE=Release
ARG PORTABLE_GLIBC=2.28
ARG OPENSSL_VERSION=3.5.8
ARG OPENSSL_SHA256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2

# Perl modules needed by the "Configure" script of OpenSSL 3
RUN dnf -y install perl-IPC-Cmd perl-Time-Piece && dnf clean all

# Static OpenSSL for the MongoDB C driver. Its default certificate store is the one
# of the system (/etc/ssl/cert.pem, /etc/ssl/certs), SSL_CERT_FILE or "tlsCAFile" override it.
RUN curl -fL https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz -o /tmp/openssl.tar.gz \
 && echo "${OPENSSL_SHA256}  /tmp/openssl.tar.gz" | sha256sum -c - \
 && mkdir /tmp/openssl && tar -xzf /tmp/openssl.tar.gz -C /tmp/openssl --strip-components=1 \
 && cd /tmp/openssl \
 && ./Configure linux-x86_64 no-shared no-module no-tests no-docs -fPIC \
      --prefix=/opt/openssl --libdir=lib --openssldir=/etc/ssl \
 && make -j"$(nproc)" && make install_sw \
 && rm -rf /tmp/openssl /tmp/openssl.tar.gz

WORKDIR /usr/share/src
COPY . /usr/share/src

# CMAKE_POLICY_VERSION_MINIMUM: the image has CMake 4, and some bundled
# third-party libraries declare an older minimum version
RUN cmake -S /usr/share/src/MongoDB -B /usr/share/build-lsb \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -DSTATIC_BUILD=ON \
      -DALLOW_DOWNLOADS=ON \
      -DAUTO_INSTALL_DEPENDENCIES=ON \
      -DBUILD_TESTS=ON \
      -DPORTABLE_BUILD=ON \
      -DOPENSSL_ROOT_DIR=/opt/openssl \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
 && cmake --build /usr/share/build-lsb -j"$(nproc)" \
 && mkdir /out \
 && cp /usr/share/build-lsb/libOrthancMongoDBIndex.so.* /out/libOrthancMongoDBIndex.so \
 && cp /usr/share/build-lsb/libOrthancMongoDBStorage.so.* /out/libOrthancMongoDBStorage.so \
 && /usr/share/src/Resources/CheckPortability.sh ${PORTABLE_GLIBC} /out/libOrthancMongoDBIndex.so /out/libOrthancMongoDBStorage.so \
 && cd /out && sha256sum libOrthancMongoDBIndex.so libOrthancMongoDBStorage.so > SHA256SUMS


FROM scratch AS lsb-artifacts

COPY --from=lsb /out/ /


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


# The orthanc-tests suite (Python 2) against Orthanc 1.13.0 (LSB binary) with the
# portable plugins, the ones that are released
FROM debian:12-slim AS integration

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

ARG ORTHANC_VERSION
ARG ORTHANC_TESTS_VERSION
ARG LSB_DOWNLOADS=https://orthanc.uclouvain.be/downloads/linux-standard-base

# The tests call the DCMTK tools (storescu, findscu...), and Orthanc needs the locale
# en_US.UTF-8 (its "Locale" option) for accent-insensitive matching
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl bzip2 mercurial patch dcmtk locales \
 && rm -rf /var/lib/apt/lists/* \
 && sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen \
 && locale-gen

# Python 2.7 with the modules of orthanc-tests, from conda-forge
RUN curl -fL https://micro.mamba.pm/api/micromamba/linux-64/latest | tar -xj -C /usr/local bin/micromamba \
 && micromamba create -y -p /opt/py27 -c conda-forge python=2.7 numpy pillow pip \
 && /opt/py27/bin/pip install --no-cache-dir httplib2==0.22.0 pydicom==1.4.2 \
 && micromamba clean -y --all

WORKDIR /opt/integration

RUN curl -fL ${LSB_DOWNLOADS}/orthanc/${ORTHANC_VERSION}/Orthanc -o /opt/integration/Orthanc \
 && chmod +x /opt/integration/Orthanc

RUN hg clone -u ${ORTHANC_TESTS_VERSION} https://orthanc.uclouvain.be/hg/orthanc-tests/ /opt/integration/orthanc-tests

COPY .docker/integration/ /opt/integration/
RUN patch -d /opt/integration/orthanc-tests -p1 < /opt/integration/orthanc-tests.patch

# In CI, "lsb-artifacts" is replaced by the plugins of the lsb-build job ("--build-context lsb-artifacts=<dir>")
COPY --from=lsb-artifacts /libOrthancMongoDBIndex.so /libOrthancMongoDBStorage.so /opt/integration/plugins/

ENTRYPOINT ["/opt/integration/run-orthanc-tests.sh"]
