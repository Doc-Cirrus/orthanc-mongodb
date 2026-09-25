#!/bin/sh

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


# Runs the unit tests built with ENABLE_COVERAGE against a standalone server
# and a replica set, then writes the coverage report of the code of this
# repository ("MongoDB/" and "Framework/", without the tests):
#   <output>/coverage.xml   Cobertura XML, e.g. for Codecov
#   <output>/coverage.txt   summary
# The status is the one of the unit tests.
#
# Usage: Coverage.sh <standalone URI> <replica-set URI> <output folder>
#   [source folder, default: /usr/share/src] [build folder, default: /usr/share/build-coverage]

set -u

if [ $# -lt 3 ]; then
    echo "Usage: $0 <standalone URI> <replica-set URI> <output folder> [source folder] [build folder]" >&2
    exit 2
fi

STANDALONE=$1
REPLICA_SET=$2
OUTPUT=$3
SOURCE=${4:-/usr/share/src}
BUILD=${5:-/usr/share/build-coverage}

mkdir -p "$OUTPUT"
STATUS=0

# Both runs add to the same counters, so the report covers the code paths of both servers
find "$BUILD" -name '*.gcda' -delete
"$BUILD/UnitTests" "$STANDALONE" || STATUS=1
"$BUILD/UnitTests" "$REPLICA_SET" || STATUS=1

gcovr --root "$SOURCE" "$BUILD" \
      --filter "$SOURCE/MongoDB/" \
      --filter "$SOURCE/Framework/" \
      --exclude "$SOURCE/MongoDB/UnitTests/" \
      --exclude "$SOURCE/Framework/Plugins/IndexUnitTests.h" \
      --gcov-ignore-parse-errors=negative_hits.warn \
      --merge-mode-functions=separate \
      --xml-pretty --xml "$OUTPUT/coverage.xml" \
      --txt-summary --txt "$OUTPUT/coverage.txt" \
      --print-summary || STATUS=1

exit $STATUS
