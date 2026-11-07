#!/usr/bin/env python3

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


"""
Entry point of the "dicom-sender" service of
"docker-compose.override.runtime.yaml.example": downloads the samples,
then sends them to Orthanc, on the first "docker compose up" only.

Once the seed is done, a marker file is written next to the samples
("$SAMPLES_DIR/.seeded"), and the later runs exit at once, so the
service does nothing on the next "docker compose up". "--force" seeds
again. If Orthanc already stores at least as many instances as the seed
would add (e.g. an existing database), nothing is sent, and the marker
is written too.

Settings (environment variables):
  SEED_ENABLED     "false" to never seed (default: true)
  ORTHANC_URL      REST API of Orthanc (default: http://orthanc:8042)
  ORTHANC_HOST     DICOM host of Orthanc (default: orthanc)
  ORTHANC_PORT     DICOM port of Orthanc (default: 4242)
  ORTHANC_AET      AE title of Orthanc (default: ORTHANC)
  SEED_SOURCES     comma-separated identifiers of dicom-sources.json (default: all)
  SEED_COPIES      number of copies of the samples (default: 1)
  SEED_PROCESSES   number of parallel associations (default: 2)
"""


import argparse
import datetime
import json
import os
import subprocess
import sys
import time
import urllib.request


SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))


def setting(name, default):
    value = os.environ.get(name, '')
    return value if value else default


def count_instances(url):
    """Number of instances stored in Orthanc, waiting for its REST API"""
    for attempt in range(60):
        try:
            with urllib.request.urlopen(url + '/statistics', timeout=10) as response:
                return json.load(response)['CountInstances']
        except Exception as e:
            if attempt == 59:
                raise
            print('Waiting for Orthanc (%s)' % e, flush=True)
            time.sleep(5)


def write_marker(marker, details):
    os.makedirs(os.path.dirname(marker), exist_ok=True)
    with open(marker, 'w') as f:
        json.dump(dict(details, date=datetime.datetime.now().isoformat(timespec='seconds')), f, indent=1)
        f.write('\n')


def main():
    parser = argparse.ArgumentParser(description='Seeds Orthanc with the sample studies, on the first run only')
    parser.add_argument('--force', action='store_true', help='Seed again, even if it was already done')
    args = parser.parse_args()

    if setting('SEED_ENABLED', 'true').lower() in ('false', '0', 'no'):
        print('Seeding is disabled (SEED_ENABLED=false)')
        return 0

    samples = setting('SAMPLES_DIR', os.path.join(os.path.expanduser('~'), '.cache', 'orthanc-mongodb', 'samples'))
    marker = os.path.join(samples, '.seeded')

    if os.path.exists(marker) and not args.force:
        with open(marker) as f:
            print('Orthanc was already seeded, nothing to do (%s): %s' % (marker, ' '.join(f.read().split())))
        return 0

    url = setting('ORTHANC_URL', 'http://orthanc:8042').rstrip('/')
    sources = setting('SEED_SOURCES', '')
    copies = int(setting('SEED_COPIES', '1'))
    processes = setting('SEED_PROCESSES', '2')

    fetch = [sys.executable, os.path.join(SCRIPT_DIR, 'fetch-samples.py')]
    if sources:
        fetch += ['--source', sources]
    if subprocess.call(fetch) != 0:
        print('Some samples could not be downloaded, the others are sent', file=sys.stderr, flush=True)

    with open(os.path.join(SCRIPT_DIR, 'dicom-sources.json')) as f:
        entries = json.load(f)
    selected = set(s for s in sources.split(',') if s)
    expected = copies * sum(e['instances'] for e in entries if not selected or e['id'] in selected)

    details = {'sources': sources or 'all', 'copies': copies, 'instances': expected}

    stored = count_instances(url)
    if stored >= expected:
        print('Orthanc already stores %d instances, at least the %d of the seed: nothing to send' % (stored, expected))
        write_marker(marker, details)
        return 0

    send = [sys.executable, os.path.join(SCRIPT_DIR, 'dicom-sender.py'),
            '--host', setting('ORTHANC_HOST', 'orthanc'),
            '--port', setting('ORTHANC_PORT', '4242'),
            '--aet', setting('ORTHANC_AET', 'ORTHANC'),
            '--copies', str(copies),
            '--processes', processes,
            '--quiet']
    if sources:
        send += ['--source', sources]

    status = subprocess.call(send)
    if status == 0:
        write_marker(marker, details)
    else:
        print('Seeding failed, it will run again on the next start', file=sys.stderr)
    return status


if __name__ == '__main__':
    sys.exit(main())
