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
Downloads the sample DICOM studies listed in "dicom-sources.json" into
a cache folder, checks them, and unpacks them into "<cache>/dicom/<id>",
the default input of "dicom-sender.py". Nothing is downloaded again if
a sample is already there and complete.

The archives of some sources are generated on each request (TCIA), so
their bytes change from one download to the next. The checksum of a
sample ("dicomSha256") is therefore computed on the DICOM files it
contains: the SHA-256 of the sorted list of the SHA-256 of these files,
one hexadecimal digest per line.

"--describe" computes the other fields of the manifest (modalities, SOP
classes, counts...) from the downloaded files, and writes them into the
manifest: run it after adding a source.
"""


import argparse
import collections
import datetime
import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile

from pydicom import dcmread
from pydicom.errors import InvalidDicomError


SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
DEFAULT_MANIFEST = os.path.join(SCRIPT_DIR, 'dicom-sources.json')
DEFAULT_CACHE = os.environ.get('SAMPLES_DIR',
                               os.path.join(os.path.expanduser('~'), '.cache', 'orthanc-mongodb', 'samples'))

COMPLETE_MARKER = '.complete'
DOWNLOAD_ATTEMPTS = 3

# Fields that every entry of the manifest has, with their type
REQUIRED_FIELDS = {
    'id': str,
    'title': str,
    'source': str,
    'page': str,
    'license': str,
    'licenseUrl': str,
    'citation': str,
    'urls': list,
}

# Fields computed by "--describe"
DESCRIBED_FIELDS = {
    'dicomSha256': str,
    'size': int,
    'modalities': list,
    'sopClasses': list,
    'transferSyntaxes': list,
    'studies': int,
    'series': int,
    'instances': int,
    'deidentified': bool,
    'fetched': str,
}


def check_manifest(entries):
    """Checks the shape of the manifest, returns a list of errors"""
    errors = []
    ids = set()

    if not isinstance(entries, list):
        return ['The manifest must be a JSON array']

    for i, entry in enumerate(entries):
        name = entry.get('id', '#%d' % i) if isinstance(entry, dict) else '#%d' % i

        if not isinstance(entry, dict):
            errors.append('%s: not a JSON object' % name)
            continue

        for field, kind in list(REQUIRED_FIELDS.items()) + list(DESCRIBED_FIELDS.items()):
            if field not in entry:
                if field in REQUIRED_FIELDS:
                    errors.append('%s: missing "%s"' % (name, field))
            elif not isinstance(entry[field], kind) or (kind is int and isinstance(entry[field], bool)):
                errors.append('%s: "%s" must be of type %s' % (name, field, kind.__name__))

        if entry.get('id') in ids:
            errors.append('%s: duplicate identifier' % name)
        ids.add(entry.get('id'))

        if not entry.get('urls'):
            errors.append('%s: "urls" is empty' % name)

    return errors


def is_described(entry):
    return all(field in entry for field in DESCRIBED_FIELDS)


def select_entries(entries, sources, modalities):
    selected = []
    for entry in entries:
        if sources and entry['id'] not in sources:
            continue
        if modalities and not set(entry.get('modalities', [])) & modalities:
            continue
        selected.append(entry)
    return selected


def download(url, target):
    """Downloads a URL into a file, retrying on errors"""
    for attempt in range(1, DOWNLOAD_ATTEMPTS + 1):
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'orthanc-mongodb-fetch-samples'})
            with urllib.request.urlopen(request, timeout=120) as response, open(target, 'wb') as f:
                shutil.copyfileobj(response, f, 1024 * 1024)
            return
        except Exception as e:
            if attempt == DOWNLOAD_ATTEMPTS:
                raise
            print('    download failed (%s), retrying' % e, flush=True)
            time.sleep(5 * attempt)


def extract(archive, target):
    """Unpacks a ZIP archive, refusing paths outside of the target folder"""
    root = os.path.realpath(target)
    with zipfile.ZipFile(archive) as z:
        for member in z.namelist():
            path = os.path.realpath(os.path.join(root, member))
            if path != root and not path.startswith(root + os.sep):
                raise ValueError('Unsafe path in the archive: %s' % member)
        z.extractall(root)


def list_files(folder):
    for path, _, files in os.walk(folder):
        for name in files:
            if name != COMPLETE_MARKER:
                yield os.path.join(path, name)


def describe(folder):
    """Reads the DICOM files of a sample, returns the fields of the manifest that describe it"""
    digests = []
    modalities, sop_classes, syntaxes = set(), set(), set()
    studies, series, instances = set(), set(), set()
    size = 0
    deidentified = True

    for path in list_files(folder):
        try:
            ds = dcmread(path, stop_before_pixels=True)
        except InvalidDicomError:
            continue  # e.g. the LICENSE file of the TCIA archives
        if 'SOPClassUID' not in ds:
            continue  # e.g. DICOMDIR

        with open(path, 'rb') as f:
            content = f.read()
        digests.append(hashlib.sha256(content).hexdigest())
        size += len(content)

        modalities.add(str(ds.get('Modality', 'OT')))
        sop_classes.add(str(ds.SOPClassUID))
        syntaxes.add(str(ds.file_meta.TransferSyntaxUID) if 'TransferSyntaxUID' in ds.file_meta else '')
        studies.add(ds.get('StudyInstanceUID'))
        series.add(ds.get('SeriesInstanceUID'))
        instances.add(ds.get('SOPInstanceUID'))
        if str(ds.get('PatientIdentityRemoved', 'NO')).upper() != 'YES':
            deidentified = False

    if not digests:
        raise ValueError('No DICOM file in %s' % folder)

    return {
        'dicomSha256': hashlib.sha256(''.join(d + '\n' for d in sorted(digests)).encode('ascii')).hexdigest(),
        'size': size,
        'modalities': sorted(modalities),
        'sopClasses': sorted(sop_classes),
        'transferSyntaxes': sorted(syntaxes),
        'studies': len(studies),
        'series': len(series),
        'instances': len(instances),  # Files with the same SOP Instance UID are one instance in Orthanc
        'deidentified': deidentified,
    }


def fetch(entry, cache, force, check=True):
    """Downloads and unpacks one sample, returns its description"""
    target = os.path.join(cache, 'dicom', entry['id'])
    marker = os.path.join(target, COMPLETE_MARKER)

    if not force and os.path.exists(marker) and 'dicomSha256' in entry:
        with open(marker) as f:
            if f.read().strip() == entry['dicomSha256']:
                print('  %s: already there' % entry['id'], flush=True)
                return None

    os.makedirs(os.path.join(cache, 'dicom'), exist_ok=True)
    staging = tempfile.mkdtemp(prefix='.%s-' % entry['id'], dir=os.path.join(cache, 'dicom'))

    try:
        for i, url in enumerate(entry['urls']):
            print('  %s: downloading %d/%d' % (entry['id'], i + 1, len(entry['urls'])), flush=True)
            archive = os.path.join(staging, '%d.download' % i)
            download(url, archive)

            folder = os.path.join(staging, str(i))
            if zipfile.is_zipfile(archive):
                extract(archive, folder)
                os.remove(archive)
            else:
                # A single DICOM file, named after the last part of its URL
                os.makedirs(folder)
                name = os.path.basename(urllib.parse.urlparse(url).path) or 'file.dcm'
                os.rename(archive, os.path.join(folder, name))

        description = describe(staging)

        if check and 'dicomSha256' in entry and description['dicomSha256'] != entry['dicomSha256']:
            raise ValueError('The DICOM files differ from the manifest (dicomSha256 %s, expected %s)' %
                             (description['dicomSha256'], entry['dicomSha256']))

        with open(os.path.join(staging, COMPLETE_MARKER), 'w') as f:
            f.write(description['dicomSha256'] + '\n')

        if os.path.exists(target):
            shutil.rmtree(target)
        os.rename(staging, target)
        print('  %s: %d instances, %.1f MB' % (entry['id'], description['instances'], description['size'] / 1e6), flush=True)
        return description

    finally:
        if os.path.exists(staging):
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description='Downloads the sample DICOM studies of dicom-sources.json')
    parser.add_argument('--manifest', default=DEFAULT_MANIFEST, help='List of the samples (default: %(default)s)')
    parser.add_argument('--cache', default=DEFAULT_CACHE, help='Download folder (default: $SAMPLES_DIR or %(default)s)')
    parser.add_argument('--source', default='', help='Comma-separated identifiers of the samples to fetch (default: all)')
    parser.add_argument('--modality', default='', help='Comma-separated modalities: only the samples that contain one of them')
    parser.add_argument('--list', action='store_true', help='List the samples, and download nothing')
    parser.add_argument('--force', action='store_true', help='Download again the samples that are already there')
    parser.add_argument('--describe', action='store_true',
                        help='Write the fields computed from the downloaded files into the manifest (after adding a source)')
    args = parser.parse_args()

    with open(args.manifest) as f:
        entries = json.load(f)

    errors = check_manifest(entries)
    if errors:
        for error in errors:
            print('Invalid manifest: %s' % error, file=sys.stderr)
        return 2

    sources = set(s for s in args.source.split(',') if s)
    unknown = sources - set(e['id'] for e in entries)
    if unknown:
        print('Unknown samples: %s' % ', '.join(sorted(unknown)), file=sys.stderr)
        return 2

    selected = select_entries(entries, sources, set(m.upper() for m in args.modality.split(',') if m))

    if args.list:
        for entry in selected:
            print('%-34s %-14s %5s instances %7.1f MB  %s' % (
                entry['id'], ','.join(entry.get('modalities', [])), entry.get('instances', '?'),
                entry.get('size', 0) / 1e6, entry['license']))
        return 0

    print('Cache: %s' % args.cache, flush=True)
    failed = []
    for entry in selected:
        if not args.describe and not is_described(entry):
            print('  %s: not described yet, run with --describe' % entry['id'], file=sys.stderr)
            failed.append(entry['id'])
            continue

        try:
            description = fetch(entry, args.cache, args.force or args.describe, check=not args.describe)
        except Exception as e:
            print('  %s: FAILED: %s' % (entry['id'], e), file=sys.stderr, flush=True)
            failed.append(entry['id'])
            continue

        if args.describe and description is not None:
            entry.update(description)
            entry['fetched'] = datetime.date.today().isoformat()

    if args.describe:
        with open(args.manifest, 'w') as f:
            json.dump(entries, f, indent=2)
            f.write('\n')
        print('Manifest updated: %s' % args.manifest)

    if failed:
        print('%d sample(s) failed: %s' % (len(failed), ', '.join(failed)), file=sys.stderr)
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
