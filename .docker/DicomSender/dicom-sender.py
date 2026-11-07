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
Sends DICOM files to a PACS (e.g. Orthanc) by C-STORE, as new studies.

Each copy sends all the input files again: every study gets new Study
and Series Instance UIDs, and a new random patient (ID, name, birth
date, sex, study ID and date), so the copies are distinct studies of
distinct patients with the same images. The copies are spread over
several processes, each with its own association.

The SOP Instance UIDs are kept unless "--new-sop-uids" is given, so the
copies share them (as in the first release of this script).
"""


import argparse
import collections
import json
import os
import random
import string
import sys
import time
from datetime import datetime
from multiprocessing import Pool

from pydicom import dcmread
from pydicom.errors import InvalidDicomError
from pydicom.uid import generate_uid
from pynetdicom import AE


DEFAULT_INPUT = os.path.join(os.environ.get('SAMPLES_DIR',
                                            os.path.join(os.path.expanduser('~'), '.cache', 'orthanc-mongodb', 'samples')),
                             'dicom')

MAX_PRESENTATION_CONTEXTS = 128  # Limit of the DICOM standard for one association

# Global state of each worker process, set by "init_worker()"
OPTIONS = None
FILES = None
CONTEXTS = None


def get_random_string(length):
    return ''.join(random.choice(string.ascii_uppercase) for _ in range(length))


def random_date():
    return datetime.fromtimestamp(random.randint(1, int(time.time()))).strftime('%Y%m%d')


def list_dicom_files(folder, sources):
    """
    Lists the DICOM files of a folder, with their SOP class and transfer
    syntax. "sources" limits them to these subfolders (e.g. the
    identifiers of "dicom-sources.json"). DICOMDIR and non-DICOM files
    (e.g. the LICENSE of the samples) are skipped.
    """
    files = []

    for path, dirs, names in os.walk(folder):
        dirs.sort()
        if sources and os.path.relpath(path, folder) == '.':
            dirs[:] = [d for d in dirs if d in sources]

        for name in sorted(names):
            full = os.path.join(path, name)
            try:
                ds = dcmread(full, stop_before_pixels=True)
            except (InvalidDicomError, OSError):
                continue

            if 'SOPClassUID' not in ds or 'TransferSyntaxUID' not in ds.file_meta:
                continue  # DICOMDIR, or a file without meta header

            files.append((full, str(ds.SOPClassUID), str(ds.file_meta.TransferSyntaxUID), os.path.getsize(full)))

    return files


def get_presentation_contexts(files):
    """
    One presentation context per SOP class and transfer syntax of the
    input, so that each file is sent as it is, without transcoding
    """
    pairs = sorted(set((sop_class, syntax) for _, sop_class, syntax, _ in files))

    if len(pairs) > MAX_PRESENTATION_CONTEXTS:
        raise ValueError('The input has %d combinations of SOP class and transfer syntax, more than the %d '
                         'presentation contexts of an association: send it in several parts' %
                         (len(pairs), MAX_PRESENTATION_CONTEXTS))

    return pairs


class Copy:
    """New UIDs and patients of one copy of the input"""

    def __init__(self, new_sop_uids):
        self.studies = {}
        self.series = {}
        self.instances = {} if new_sop_uids else None

    def apply(self, ds):
        study = ds.get('StudyInstanceUID')
        if study not in self.studies:
            self.studies[study] = {
                'StudyInstanceUID': generate_uid(),
                'StudyID': get_random_string(6),
                'StudyDate': random_date(),
                'PatientID': get_random_string(6),
                'PatientName': '%s^%s' % (get_random_string(5), get_random_string(5)),
                'PatientBirthDate': random_date(),
                'PatientSex': random.choice(['M', 'F', 'O']),
            }

        series = ds.get('SeriesInstanceUID')
        if series not in self.series:
            self.series[series] = generate_uid()

        for key, value in self.studies[study].items():
            setattr(ds, key, value)
        ds.SeriesInstanceUID = self.series[series]

        if self.instances is not None:
            instance = ds.SOPInstanceUID
            if instance not in self.instances:
                self.instances[instance] = generate_uid()
            ds.SOPInstanceUID = self.instances[instance]
            ds.file_meta.MediaStorageSOPInstanceUID = self.instances[instance]


def init_worker(options, files, contexts):
    # Runs in each worker process, so that this state also reaches the workers started with "spawn" (Windows)
    global OPTIONS, FILES, CONTEXTS
    OPTIONS, FILES, CONTEXTS = options, files, contexts


def associate():
    ae = AE(ae_title=OPTIONS['calling_aet'])
    ae.acse_timeout = OPTIONS['timeout']
    ae.dimse_timeout = OPTIONS['timeout']
    ae.network_timeout = OPTIONS['timeout']

    for sop_class, syntax in CONTEXTS:
        ae.add_requested_context(sop_class, syntax)

    return ae.associate(OPTIONS['host'], OPTIONS['port'], ae_title=OPTIONS['aet'])


def send_copy(index):
    """Sends all the files once, as new studies. Returns one row per file."""
    copy = Copy(OPTIONS['new_sop_uids'])
    rows = []
    assoc = None

    for path, sop_class, syntax, size in FILES:
        if assoc is None or not assoc.is_established:
            assoc = associate()
            if not assoc.is_established:
                print('Copy %d: association rejected, aborted or never connected' % index, flush=True)
                rows.append({'copy': index, 'file': path, 'bytes': size, 'seconds': 0, 'status': 'no association'})
                assoc = None
                continue

        ds = dcmread(path)
        copy.apply(ds)

        start = time.time()
        try:
            try:
                status = assoc.send_c_store(ds)
            except RuntimeError:
                # The peer aborted the association after the previous file, and pynetdicom
                # noticed only now: associate again and send this file once more
                assoc = associate()
                if not assoc.is_established:
                    raise
                start = time.time()
                status = assoc.send_c_store(ds)
            code = '0x%04X' % status.Status if status else 'no response'
        except ValueError:
            # No accepted presentation context for this SOP class and transfer syntax
            code = 'not accepted'
        except RuntimeError:
            code = 'no association'
            assoc = None
        seconds = time.time() - start

        rows.append({'copy': index, 'file': path, 'bytes': size, 'seconds': seconds, 'status': code})

        if not OPTIONS['quiet']:
            print('Copy %d: %s %s' % (index, code, path), flush=True)

    if assoc is not None and assoc.is_established:
        assoc.release()

    return rows


def is_success(code):
    # 0x0000: success; 0xB000, 0xB006, 0xB007: warnings (the instance is stored)
    return code in ('0x0000', '0xB000', '0xB006', '0xB007')


def percentile(values, p):
    if not values:
        return 0
    values = sorted(values)
    return values[min(len(values) - 1, int(round(p / 100.0 * (len(values) - 1))))]


def main():
    parser = argparse.ArgumentParser(description='Sends DICOM files to a PACS by C-STORE, as new studies and patients')
    parser.add_argument('--input', default=DEFAULT_INPUT, help='Folder of the DICOM files (default: %(default)s)')
    parser.add_argument('--source', default='', help='Comma-separated subfolders of the input to send (default: all)')
    parser.add_argument('--host', default='localhost', help='Host of the PACS (default: %(default)s)')
    parser.add_argument('--port', type=int, default=4242, help='DICOM port of the PACS (default: %(default)s)')
    parser.add_argument('--aet', default='ORTHANC', help='AE title of the PACS (default: %(default)s)')
    parser.add_argument('--calling-aet', default='DICOM_SENDER', help='AE title of this script (default: %(default)s)')
    parser.add_argument('--processes', type=int, default=2, help='Number of parallel associations (default: %(default)s)')
    parser.add_argument('--copies', type=int, default=4, help='Number of times the input is sent (default: %(default)s)')
    parser.add_argument('--new-sop-uids', action='store_true', help='Give new SOP Instance UIDs to each copy')
    parser.add_argument('--timeout', type=float, default=60, help='Network timeout in seconds (default: %(default)s)')
    parser.add_argument('--quiet', action='store_true', help='Only print the summary, not one line per file')
    parser.add_argument('--report', help='Write the summary and one row per C-STORE to this JSON file')
    args = parser.parse_args()

    sources = set(s for s in args.source.split(',') if s)
    files = list_dicom_files(args.input, sources)
    if not files:
        print('No DICOM file in %s' % args.input, file=sys.stderr)
        return 2

    try:
        contexts = get_presentation_contexts(files)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 2

    options = {
        'host': args.host, 'port': args.port, 'aet': args.aet, 'calling_aet': args.calling_aet,
        'new_sop_uids': args.new_sop_uids, 'timeout': args.timeout, 'quiet': args.quiet,
    }

    print('Sending %d files (%.1f MB) %d times to %s@%s:%d with %d processes' % (
        len(files), sum(f[3] for f in files) / 1e6, args.copies, args.aet, args.host, args.port, args.processes),
        flush=True)

    start = time.time()
    with Pool(processes=args.processes, initializer=init_worker, initargs=(options, files, contexts)) as pool:
        rows = [row for rows in pool.map(send_copy, range(1, args.copies + 1), chunksize=1) for row in rows]
    elapsed = time.time() - start

    statuses = collections.Counter(row['status'] for row in rows)
    stored = [row for row in rows if is_success(row['status'])]
    latencies = [row['seconds'] for row in stored]

    summary = {
        'files': len(rows),
        'stored': len(stored),
        'failed': len(rows) - len(stored),
        'statuses': dict(statuses),
        'seconds': elapsed,
        'instancesPerSecond': len(stored) / elapsed if elapsed > 0 else 0,
        'megabytesPerSecond': sum(row['bytes'] for row in stored) / 1e6 / elapsed if elapsed > 0 else 0,
        'latencyP50': percentile(latencies, 50),
        'latencyP95': percentile(latencies, 95),
        'latencyP99': percentile(latencies, 99),
    }

    print('Stored %d of %d files in %.1f s: %.1f instances/s, %.1f MB/s, store latency p50 %.0f ms, p95 %.0f ms' % (
        summary['stored'], summary['files'], elapsed, summary['instancesPerSecond'], summary['megabytesPerSecond'],
        summary['latencyP50'] * 1000, summary['latencyP95'] * 1000))

    if summary['failed']:
        failures = ', '.join('%s: %d' % (code, count) for code, count in sorted(statuses.items()) if not is_success(code))
        print('Failed: %s' % failures, file=sys.stderr)

    if args.report:
        with open(args.report, 'w') as f:
            json.dump({'summary': summary, 'stores': rows}, f, indent=1)

    return 1 if summary['failed'] else 0


if __name__ == '__main__':
    sys.exit(main())
