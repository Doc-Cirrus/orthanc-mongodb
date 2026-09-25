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


# Image of the "dicom-sender" service (docker-compose.override.runtime.yaml.example).
# Build context: this folder. The samples are downloaded at run time into /samples
# (a volume), never into the image.

FROM python:3.12-slim

WORKDIR /opt/dicom-sender

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY dicom-sender.py fetch-samples.py seed.py dicom-sources.json ./

ENV SAMPLES_DIR=/samples
VOLUME /samples

# Downloads the samples and sends them to Orthanc once; the scripts can also be run
# directly, e.g. "docker compose run --rm dicom-sender python3 dicom-sender.py --help"
CMD ["python3", "seed.py"]
