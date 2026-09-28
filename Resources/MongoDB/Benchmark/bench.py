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
Times typical requests of the Orthanc REST API (Explorer 2 study
lists, "/tools/find", "/tools/count-resources", paged lists) against
an Orthanc whose index is a database filled by "GenerateIndex.js".
The resource identifiers below are those of this script, with any
size of at least 10,000 patients.

Orthanc must run with the index plugin only ("EnableStorage": false),
and with "LimitFindResults" and "LimitFindInstances" set to 0. Run
it once with "EnableExtendedFind": true and once with false, to
compare the integrated Find with the generic Find of Orthanc:

  ./bench.py --url http://localhost:8042 --output on.json
  ./bench.py --url http://localhost:8042 --output off.json --repeat 1

Each request is sent once to warm up, then "--repeat" times, and the
median time is printed. Without "EnableExtendedFind", the requests
with "OrderBy" answer HTTP 500, as the generic Find cannot sort.
"""


import argparse
import json
import statistics
import time
import urllib.error
import urllib.request


def Call(base, method, path, body, timeout):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(base + path, data=data, method=method)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read())
    except urllib.error.HTTPError as e:
        return {"HTTP": e.code}
    except TimeoutError:
        return {"Timeout": timeout}


def Summarize(answer):
    # The number of answers, the count, or the HTTP error
    if isinstance(answer, list):
        return len(answer)
    elif isinstance(answer, dict) and ("Count" in answer or "HTTP" in answer or "Timeout" in answer):
        return answer
    else:
        return "object"


LATEST_STUDIES = [{"Type": "DicomTag", "Key": "StudyDate", "Direction": "DESC"}]

REQUESTS = [
    ("Studies by patient name prefix, expand", "POST", "/tools/find",
     {"Level": "Study", "Query": {"PatientName": "NAME01234*"}, "Expand": True}),
    ("Studies in one week, expand", "POST", "/tools/find",
     {"Level": "Study", "Query": {"StudyDate": "20180101-20180107"}, "Expand": True}),
    ("CT studies, limit 100, expand", "POST", "/tools/find",
     {"Level": "Study", "Query": {"ModalitiesInStudy": "CT"}, "Limit": 100, "Expand": True}),
    ("MR series in one month, expand", "POST", "/tools/find",
     {"Level": "Series", "Query": {"Modality": "MR", "StudyDate": "20190601-20190630"}, "Expand": True}),
    ("Instances of one study", "POST", "/tools/find",
     {"Level": "Instance", "Query": {"StudyInstanceUID": "1.2.48147"}}),
    ("GET /studies?expand&limit=100&since=10000", "GET", "/studies?expand&limit=100&since=10000", None),
    ("GET /instances?limit=100&since=500000", "GET", "/instances?limit=100&since=500000", None),
    ("GET /studies/{id}", "GET", "/studies/s1234_1", None),
    ("GET /patients/{id}", "GET", "/patients/p1234", None),
    ("GET /series/{id}", "GET", "/series/e48148", None),
    ("Count the studies of one year", "POST", "/tools/count-resources",
     {"Level": "Study", "Query": {"StudyDate": "20180101-20181231"}}),
    ("OE2: latest 100 studies, with tags", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Limit": 100, "Expand": True, "OrderBy": LATEST_STUDIES,
      "RequestedTags": ["ModalitiesInStudy", "NumberOfStudyRelatedSeries", "NumberOfStudyRelatedInstances"]}),
    ("OE2: latest 100 CT studies", "POST", "/tools/find",
     {"Level": "Study", "Query": {"ModalitiesInStudy": "CT"}, "Limit": 100, "Expand": True, "OrderBy": LATEST_STUDIES,
      "RequestedTags": ["ModalitiesInStudy", "NumberOfStudyRelatedInstances"]}),
    ("OE2: latest 100 studies of one year", "POST", "/tools/find",
     {"Level": "Study", "Query": {"StudyDate": "20180101-20181231"}, "Limit": 100, "Expand": True,
      "OrderBy": LATEST_STUDIES}),
    ("OE2: latest 100 studies of a name prefix", "POST", "/tools/find",
     {"Level": "Study", "Query": {"PatientName": "NAME0*"}, "Limit": 100, "Expand": True,
      "OrderBy": LATEST_STUDIES}),
    ("OE2: 100 studies without ordering", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Limit": 100, "Expand": True,
      "RequestedTags": ["ModalitiesInStudy", "NumberOfStudyRelatedSeries", "NumberOfStudyRelatedInstances"]}),
]


def Main():
    parser = argparse.ArgumentParser(description="Times typical Orthanc requests on a database of GenerateIndex.js")
    parser.add_argument("--url", default="http://localhost:8042", help="Base URL of the Orthanc REST API")
    parser.add_argument("--repeat", type=int, default=3, help="Timed runs of each request, after one warm-up")
    parser.add_argument("--timeout", type=float, default=600, help="Seconds before a request is given up (default: %(default)s)")
    parser.add_argument("--output", help="JSON file receiving the median times (ms) and the answers")
    args = parser.parse_args()

    results = {}
    for name, method, path, body in REQUESTS:
        answer = Summarize(Call(args.url, method, path, body, args.timeout))  # Warm-up

        if isinstance(answer, dict) and "Timeout" in answer:
            # Not repeated: each run would take the whole timeout
            results[name] = {"milliseconds": None, "answer": answer}
            print("%-45s  > %6.0f s   %s" % (name, args.timeout, json.dumps(answer)), flush=True)
            continue

        times = []
        for i in range(args.repeat):
            start = time.time()
            Call(args.url, method, path, body, args.timeout)
            times.append((time.time() - start) * 1000)

        results[name] = {"milliseconds": statistics.median(times), "answer": answer}
        print("%-45s %9.0f ms   %s" % (name, results[name]["milliseconds"], json.dumps(answer)), flush=True)

    if args.output:
        with open(args.output, "w") as f:
            json.dump(results, f, indent=2)


if __name__ == "__main__":
    Main()
