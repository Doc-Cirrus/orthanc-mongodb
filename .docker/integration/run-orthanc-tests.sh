#!/bin/bash

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


# Runs the orthanc-tests suite against Orthanc with the MongoDB plugins, and
# compares its failures with "known-failures.txt": the script fails if a test
# fails that is not listed there, or if a listed test passes (then remove it
# from the list). After the suite, the database must be empty.
#
# Environment:
#   MONGO_URL       connection URI, with the name of an empty database (required)
#   MODE            "standalone" or "replica-set": selects the known failures (required)
#   ORTHANC         Orthanc binary (default: /opt/integration/Orthanc)
#   PLUGINS         folder of the MongoDB plugins (default: /opt/integration/plugins)
#   TESTS           checkout of orthanc-tests (default: /opt/integration/orthanc-tests)
#   PYTHON2         Python 2.7 with httplib2, numpy and pydicom 1.4 (default: /opt/py27/bin/python)
#   KNOWN_FAILURES  list of the known failures (default: next to this script)
#   RESULTS         folder of the logs (default: /results, or ./results)

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
: "${MONGO_URL:?MONGO_URL must give the MongoDB URI, with the name of an empty database}"
: "${MODE:?MODE must be standalone or replica-set}"
ORTHANC=${ORTHANC:-/opt/integration/Orthanc}
PLUGINS=${PLUGINS:-/opt/integration/plugins}
TESTS=${TESTS:-/opt/integration/orthanc-tests}
PYTHON2=${PYTHON2:-/opt/py27/bin/python}
KNOWN_FAILURES=${KNOWN_FAILURES:-$HERE/known-failures.txt}
if [ -z "${RESULTS:-}" ]; then
    if [ -d /results ]; then RESULTS=/results; else RESULTS=$PWD/results; fi
fi

case "$MODE" in
    standalone|replica-set) ;;
    *) echo "MODE must be standalone or replica-set" >&2; exit 2 ;;
esac

WORK=$(mktemp -d)
mkdir -p "$RESULTS"
REST="http://localhost:8042"
AUTH="alice:orthanctest"

# Configuration of the Orthanc under test: the one of orthanc-tests, with the MongoDB plugins
cd "$WORK" || exit 1
"$PYTHON2" "$TESTS/GenerateConfigurationForTests.py" --force --target "$WORK/base.json" > /dev/null
"$PYTHON2" - "$WORK/base.json" "$WORK/orthanc.json" "$PLUGINS" "$MONGO_URL" "$WORK" <<'EOF'
import json, sys
source, target, plugins, uri, work = sys.argv[1:]
with open(source) as f:
    config = json.load(f)
config['Plugins'] = [ plugins ]
config['StorageDirectory'] = work + '/storage'
config['IndexDirectory'] = work + '/storage'
config['MongoDB'] = { 'EnableIndex' : True, 'EnableStorage' : True, 'ConnectionUri' : uri }
with open(target, 'w') as f:
    json.dump(config, f, indent = 1)
EOF

"$ORTHANC" "$WORK/orthanc.json" > "$RESULTS/orthanc-$MODE.log" 2>&1 &
PID=$!

for _ in $(seq 120); do
    curl -sf -u "$AUTH" "$REST/system" > /dev/null && break
    if ! kill -0 $PID 2> /dev/null; then
        echo "Orthanc stopped at startup, see $RESULTS/orthanc-$MODE.log" >&2
        tail -n 30 "$RESULTS/orthanc-$MODE.log" >&2
        exit 1
    fi
    sleep 1
done

echo "Running orthanc-tests ($MODE): $(curl -sf -u "$AUTH" "$REST/system" | tr -d '\n' | grep -o '"DatabaseBackendPlugin" *: *"[^"]*"')"

cd "$TESTS" || exit 1
"$PYTHON2" Tests/Run.py --force --orthanc "$ORTHANC" > "$RESULTS/tests-$MODE.log" 2>&1
echo "Run.py exit status: $?" >> "$RESULTS/tests-$MODE.log"

# After the suite, which deletes what it stores, the database must be empty
STATISTICS=$(curl -sf -u "$AUTH" "$REST/statistics" | tr -d ' \n')

kill -TERM $PID
wait $PID

STATUS=0

if ! grep -q '^Ran [0-9]* tests' "$RESULTS/tests-$MODE.log"; then
    echo "ERROR: the suite did not run to its end, see $RESULTS/tests-$MODE.log" >&2
    tail -n 30 "$RESULTS/tests-$MODE.log" >&2
    exit 1
fi

grep -E '^Ran [0-9]+ tests|^(OK|FAILED)' "$RESULTS/tests-$MODE.log"

FAILED=$(sed -n 's/^\(FAIL\|ERROR\): \([A-Za-z0-9_]*\) .*/\2/p' "$RESULTS/tests-$MODE.log" | sort -u)
KNOWN=$(awk -v mode="$MODE" '($1 == "all" || $1 == mode) && $3 != "flaky" { print $2 }' "$KNOWN_FAILURES" | sort -u)
FLAKY=$(awk -v mode="$MODE" '($1 == "all" || $1 == mode) && $3 == "flaky" { print $2 }' "$KNOWN_FAILURES" | sort -u)

for TEST in $(comm -23 <(echo "$FAILED") <(echo -e "$KNOWN\n$FLAKY" | sort -u)); do
    echo "ERROR: $TEST failed, and is not a known failure" >&2
    STATUS=1
done

for TEST in $(comm -13 <(echo "$FAILED") <(echo "$KNOWN")); do
    echo "ERROR: $TEST passed, but is listed in $(basename "$KNOWN_FAILURES"): remove it from the list" >&2
    STATUS=1
done

echo "Known failures ($MODE):" $KNOWN
if [ -n "$FLAKY" ]; then
    echo "Flaky tests ($MODE): failed:" $(comm -12 <(echo "$FAILED") <(echo "$FLAKY")) \
        "passed:" $(comm -13 <(echo "$FAILED") <(echo "$FLAKY"))
fi

for COUNT in CountPatients CountStudies CountSeries CountInstances; do
    if ! echo "$STATISTICS" | grep -q "\"$COUNT\":0,"; then
        echo "ERROR: the database is not empty after the suite: $STATISTICS" >&2
        STATUS=1
        break
    fi
done

if [ $STATUS -eq 0 ]; then
    echo "orthanc-tests ($MODE): as expected"
fi

rm -rf "$WORK"
exit $STATUS
