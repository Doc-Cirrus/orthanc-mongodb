FROM jodogne/orthanc-plugins:1.8.0
RUN apt-get update
RUN apt -y install build-essential unzip cmake make libsasl2-dev uuid-dev libssl-dev zlib1g-dev git curl
RUN apt -y install python

RUN curl -L --output orthanc.tar.gz https://www.orthanc-server.com/downloads/get.php?path=/orthanc/Orthanc-1.5.7.tar.gz
RUN tar -xzf orthanc.tar.gz

RUN git clone https://github.com/Doc-Cirrus/orthanc-mongodb.git
RUN mkdir orthanc-mongodb/build && cd orthanc-mongodb/build && cmake -DCMAKE_CXX_FLAGS='-fPIC' -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DORTHANC_ROOT=/root/Orthanc-1.5.7 -DAUTO_INSTALL_DEPENDENCIES=ON .. && make

RUN rm /etc/orthanc/orthanc.json
COPY "./orthanc-conf.json" "/etc/orthanc/orthanc-conf.json"

EXPOSE 4242
EXPOSE 8042

ENTRYPOINT ["Orthanc", "/etc/orthanc/"]

