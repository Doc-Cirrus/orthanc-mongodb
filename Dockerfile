FROM oraclelinux:9 AS build

RUN dnf config-manager --enable ol9_addons
RUN yum -y install patch \
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
 gcc-c++

WORKDIR /usr/share/src
ADD . /usr/share/src

RUN mkdir -p build
RUN cd build
RUN cmake /usr/share/src/MongoDB -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/usr/local -DSTATIC_BUILD=ON -DAUTO_INSTALL_DEPENDENCIES=ON

RUN make

FROM build AS runtime

ENV MONGO_URL=mongodb://host.docker.internal:27017/inpacs?retryWrites=false

WORKDIR /usr/share/runtime

RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/orthanc/1.11.3/Orthanc -o Orthanc
RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/orthanc/1.11.3/libServeFolders.so -o libServeFolders.so
RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/orthanc/1.11.3/libModalityWorklists.so -o libModalityWorklists.so
RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/orthanc-explorer-2/1.2.1/libOrthancExplorer2.so -o libOrthancExplorer2.so
RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/orthanc-explorer-2/1.2.1/dist.zip -o dist.zip
RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/stone-web-viewer/2.5/libStoneWebViewer.so -o libStoneWebViewer.so
RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/stone-web-viewer/2.5/wasm-binaries.zip -o wasm-binaries.zip
RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/orthanc-dicomweb/1.10/libOrthancDicomWeb.so -o libOrthancDicomWeb.so
RUN curl https://orthanc.uclouvain.be/downloads/linux-standard-base/orthanc-dicomweb/1.10/libOrthancDicomWeb.so -o libOrthancDicomWeb.so
RUN cp /usr/share/src/build/libOrthancMongoFramework.a .
RUN cp /usr/share/src/build/libOrthancMongoDBIndex.so.1.11.3 ./libOrthancMongoDBIndex.so
RUN cp /usr/share/src/build/libOrthancMongoDBStorage.so.1.11.3 ./libOrthancMongoDBStorage.so

RUN chmod +x ./Orthanc
RUN unzip dist.zip
RUN unzip wasm-binaries.zip
RUN cp /usr/share/src/Resources/Config/configuration.json .
RUN sed -i 's+CONNECTION_URI+'"$MONGO_URL"'+g' ./configuration.json

EXPOSE 4242
EXPOSE 8042

CMD ./Orthanc ./configuration.json