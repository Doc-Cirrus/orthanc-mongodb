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


# Checks that plugins built with PORTABLE_BUILD load on any x86_64 Linux
# with glibc >= the given version:
#   - the only shared libraries they need are those of glibc;
#   - they use no glibc symbol newer than that version;
#   - they export nothing but the entry points of an Orthanc plugin.
#
# Usage: CheckPortability.sh <glibc version, e.g. 2.28> <plugin.so>...

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <maximum glibc version> <plugin.so>..." >&2
    exit 2
fi

GLIBC_MAX=$1
shift

# The libraries of glibc (libresolv is used for the "mongodb+srv://" URIs)
ALLOWED="libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 libresolv.so.2 ld-linux-x86-64.so.2"

STATUS=0

for PLUGIN in "$@"; do
    echo "== $PLUGIN"

    for LIB in $(readelf -d "$PLUGIN" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
        case " $ALLOWED " in
            *" $LIB "*) echo "   needs $LIB" ;;
            *) echo "   ERROR: needs $LIB, which is not part of glibc" >&2; STATUS=1 ;;
        esac
    done

    NEWEST=$(objdump -T "$PLUGIN" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -u -V | tail -n 1)
    if [ "$(printf '%s\n%s\n' "$NEWEST" "$GLIBC_MAX" | sort -V | tail -n 1)" != "$GLIBC_MAX" ]; then
        echo "   ERROR: uses glibc $NEWEST, newer than $GLIBC_MAX" >&2
        STATUS=1
    else
        echo "   newest glibc symbol: $NEWEST (maximum $GLIBC_MAX)"
    fi

    EXPORTED=$(nm -D --defined-only "$PLUGIN" | awk '{ print $3 }' | grep -v '^OrthancPlugin' || true)
    if [ -n "$EXPORTED" ]; then
        echo "   ERROR: exports other symbols than the plugin entry points:" $EXPORTED >&2
        STATUS=1
    else
        echo "   exports: $(nm -D --defined-only "$PLUGIN" | awk '{ print $3 }' | tr '\n' ' ')"
    fi
done

exit $STATUS
