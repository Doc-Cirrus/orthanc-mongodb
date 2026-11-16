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
compare the integrated Find with the generic Find of Orthanc, with
the same "--repeat", then check that both gave the same answers:

  ./bench.py --url http://localhost:8042 --output on.json
  ./bench.py --url http://localhost:8042 --output off.json
  ./bench.py --compare on.json off.json

"--compare" skips the requests that either run answered with an HTTP
error or a timeout ("not comparable"), such as the ordered requests of
the "off" run. So each run also validates its ordered pages by itself:
the keys must be monotonic, and the page must equal the same slice of
a longer ordered list (the first "Since" + "Limit" resources), which
is read once more and not timed. A failed validation exits with 1.

Each request is sent once to warm up, then "--repeat" times, and the
median time is printed. Without "EnableExtendedFind", the requests
with "OrderBy" answer HTTP 500, as the generic Find cannot sort.

Caveats:

- The "off" baseline is the generic Find of Orthanc, which reads the
  resources with one round trip each (N+1): the ratio between "on"
  and "off" measures the integrated Find against that baseline, not
  against an optimized query. Without "OrderBy", the page of the two
  modes can differ legitimately in which resources it holds, so such
  a request is only checked on its number of answers.
- The answers are checked on the identifiers ("set"), or, for an
  ordered request, on the sequence of the ordering key ("keys"),
  since the resources with an equal key come in any order.
- The indexes are those of the plugin ("MongoDBSchema::GetIndexes()"),
  created when Orthanc starts with "CreateIndexesAtStartup": true
  (the default), or by "Resources/MongoDB/Upgrades/CreateIndexes.js".
  "GenerateIndex.js" only builds a subset of them, the first start
  of Orthanc completes it (this can take a while, and it must be over
  before timing).
- The slow paths of the integrated Find ("IsOrderingScanApplicable()")
  are timed too: a label filter, a tie at the page bound, empty
  values and an ordering by metadata. The ordering casts to integer or
  float and the ordering by a tag of an ancestor level are slow paths
  too, but cannot be sent by the REST API, so they are not timed.
"""


import argparse
import hashlib
import json
import statistics
import sys
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


def Digest(values):
    return hashlib.sha1(json.dumps(values).encode()).hexdigest()

def GetId(item):
    return item["ID"] if isinstance(item, dict) else item

def Summarize(answer, key):
    # The count, a digest of the identifiers (a request without limit,
    # "key" None) or of the sequence of the ordering key (a page, as the
    # resources that tie at the bound of the page may differ), or the
    # HTTP error. A page without ordering ("key" "count") only has its count.
    if isinstance(answer, list):
        summary = {"count": len(answer)}
        if key is None:
            summary["ids"] = Digest(sorted(GetId(i) for i in answer))
        elif key != "count":
            summary["keys"] = Digest([key(i) for i in answer])
        return summary
    elif isinstance(answer, dict) and ("Count" in answer or "HTTP" in answer or "Timeout" in answer):
        return answer
    else:
        return {"object": GetId(answer) if isinstance(answer, dict) else None}

def StudyDate(item):
    return item["MainDicomTags"].get("StudyDate")

def StudyDescription(item):
    return item["MainDicomTags"].get("StudyDescription")

LATEST_STUDIES = [{"Type": "DicomTag", "Key": "StudyDate", "Direction": "DESC"}]

REQUESTS = [
    # (name, method, path, body, key): "key" is None for the answers
    # compared on their identifiers, "count" on their number only, or
    # a function of the sequence of an ordered page
    ("Studies by patient name prefix, expand", "POST", "/tools/find",
     {"Level": "Study", "Query": {"PatientName": "NAME01234*"}, "Expand": True}, None),
    ("Studies in one week, expand", "POST", "/tools/find",
     {"Level": "Study", "Query": {"StudyDate": "20180101-20180107"}, "Expand": True}, None),
    ("CT studies, limit 100, expand", "POST", "/tools/find",
     {"Level": "Study", "Query": {"ModalitiesInStudy": "CT"}, "Limit": 100, "Expand": True}, "count"),
    ("MR series in one month, expand", "POST", "/tools/find",
     {"Level": "Series", "Query": {"Modality": "MR", "StudyDate": "20190601-20190630"}, "Expand": True}, None),
    ("Instances of one study", "POST", "/tools/find",
     {"Level": "Instance", "Query": {"StudyInstanceUID": "1.2.48147"}}, None),
    ("GET /studies?expand&limit=100&since=10000", "GET", "/studies?expand&limit=100&since=10000", None, "count"),
    ("GET /instances?limit=100&since=500000", "GET", "/instances?limit=100&since=500000", None, "count"),
    ("GET /studies/{id}", "GET", "/studies/s1234_1", None, "object"),
    ("GET /patients/{id}", "GET", "/patients/p1234", None, "object"),
    ("GET /series/{id}", "GET", "/series/e48148", None, "object"),
    ("Count the studies of one year", "POST", "/tools/count-resources",
     {"Level": "Study", "Query": {"StudyDate": "20180101-20181231"}}, None),
    ("OE2: latest 100 studies, with tags", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Limit": 100, "Expand": True, "OrderBy": LATEST_STUDIES,
      "RequestedTags": ["ModalitiesInStudy", "NumberOfStudyRelatedSeries", "NumberOfStudyRelatedInstances"]}, StudyDate),
    ("OE2: latest 100 CT studies", "POST", "/tools/find",
     {"Level": "Study", "Query": {"ModalitiesInStudy": "CT"}, "Limit": 100, "Expand": True, "OrderBy": LATEST_STUDIES,
      "RequestedTags": ["ModalitiesInStudy", "NumberOfStudyRelatedInstances"]}, StudyDate),
    ("OE2: latest 100 studies of one year", "POST", "/tools/find",
     {"Level": "Study", "Query": {"StudyDate": "20180101-20181231"}, "Limit": 100, "Expand": True,
      "OrderBy": LATEST_STUDIES}, StudyDate),
    ("OE2: latest 100 studies of a name prefix", "POST", "/tools/find",
     {"Level": "Study", "Query": {"PatientName": "NAME0*"}, "Limit": 100, "Expand": True,
      "OrderBy": LATEST_STUDIES}, StudyDate),
    ("OE2: 100 studies without ordering", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Limit": 100, "Expand": True,
      "RequestedTags": ["ModalitiesInStudy", "NumberOfStudyRelatedSeries", "NumberOfStudyRelatedInstances"]}, "count"),

    # Ties and empty values: "StudyDescription" has 100 values for the
    # studies (10% have none, 10% an empty one), so the bound of the page
    # is shared by hundreds of studies
    ("Ties: first 100 studies by description", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Limit": 100, "Expand": True,
      "OrderBy": [{"Type": "DicomTag", "Key": "StudyDescription", "Direction": "ASC"}]}, StudyDescription),
    ("Ties: last 100 studies by description", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Limit": 100, "Expand": True,
      "OrderBy": [{"Type": "DicomTag", "Key": "StudyDescription", "Direction": "DESC"}]}, StudyDescription),
    ("Ties: page 100 from 20,000 by description", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Limit": 100, "Since": 20000, "Expand": True,
      "OrderBy": [{"Type": "DicomTag", "Key": "StudyDescription", "Direction": "ASC"}]}, StudyDescription),

    # Slow paths of the integrated Find: the label "TODO" is on 30% of the
    # studies, and the metadata 1024 holds a unique number per study
    ("Slow: 100 studies with a label, latest", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Labels": ["TODO"], "LabelsConstraint": "Any", "Limit": 100,
      "Expand": True, "OrderBy": LATEST_STUDIES}, StudyDate),
    ("Slow: count the studies with a label", "POST", "/tools/count-resources",
     {"Level": "Study", "Query": {}, "Labels": ["TODO"], "LabelsConstraint": "Any"}, None),
    ("Slow: 100 studies ordered by metadata", "POST", "/tools/find",
     {"Level": "Study", "Query": {}, "Limit": 100, "Expand": True,
      "OrderBy": [{"Type": "Metadata", "Key": "1024", "Direction": "ASC"}]}, GetId),
]

def Validate(base, method, path, body, key, timeout):
    # Checks an ordered page on its own, as the generic Find cannot give
    # a baseline. Returns None if it does not apply, "ok", or the problem.
    if (not isinstance(body, dict) or not body.get("OrderBy") or not callable(key) or key is GetId or
            "Limit" not in body):
        return None

    page = Call(base, method, path, body, timeout)
    if not isinstance(page, list):
        return None

    # A resource without the tag sorts last, in both directions
    def Sortable(i):
        k = key(i)
        return (1, "") if k is None else (0, k)

    keys = [Sortable(i) for i in page]
    descending = body["OrderBy"][0].get("Direction", "ASC").upper() == "DESC"
    if descending:
        expected = sorted([k for k in keys if k[0] == 0], reverse=True) + [k for k in keys if k[0] == 1]
    else:
        expected = sorted(keys)
    if keys != expected:
        return "keys are not monotonic"

    # The order is total (the ties are broken by the public ID), so the
    # page must be the slice of the longer list, identifiers included
    since = body.get("Since", 0)
    longer = dict(body)
    longer["Limit"] = since + body["Limit"]
    longer.pop("Since", None)
    prefix = Call(base, method, path, longer, timeout)
    if not isinstance(prefix, list):
        return None
    if [GetId(i) for i in page] != [GetId(i) for i in prefix[since:]]:
        return "the page is not the slice of the longer ordered list"
    return "ok"

def Compare(first, second):
    # The answers of the requests that both runs completed must be identical
    with open(first) as f:
        a = json.load(f)
    with open(second) as f:
        b = json.load(f)

    failures = 0
    for name in a:
        if name not in b:
            continue
        if any(isinstance(r[name]["answer"], dict) and ("HTTP" in r[name]["answer"] or "Timeout" in r[name]["answer"])
               for r in (a, b)):
            print("not comparable: %s" % name)
        elif a[name]["answer"] != b[name]["answer"]:
            failures += 1
            print("DIFFERENT: %s\n  %s\n  %s" % (name, json.dumps(a[name]["answer"]), json.dumps(b[name]["answer"])))

    print("%d request(s) with different answers" % failures)
    return 1 if failures else 0

def Main():
    parser = argparse.ArgumentParser(description="Times typical Orthanc requests on a database of GenerateIndex.js")
    parser.add_argument("--url", default="http://localhost:8042", help="Base URL of the Orthanc REST API")
    parser.add_argument("--repeat", type=int, default=3, help="Timed runs of each request, after one warm-up")
    parser.add_argument("--timeout", type=float, default=600, help="Seconds before a request is given up (default: %(default)s)")
    parser.add_argument("--output", help="JSON file receiving the median times (ms) and the answers")
    parser.add_argument("--compare", nargs=2, metavar=("ON", "OFF"), help="Compares the answers of two --output files, and exits")
    args = parser.parse_args()

    if args.compare:
        sys.exit(Compare(*args.compare))

    results = {}
    invalid = 0
    for name, method, path, body, key in REQUESTS:
        answer = Summarize(Call(args.url, method, path, body, args.timeout), key)  # Warm-up

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

        validation = Validate(args.url, method, path, body, key, args.timeout)
        if validation is not None:
            results[name]["validation"] = validation
            if validation != "ok":
                invalid += 1
                print("INVALID: %s: %s" % (name, validation), flush=True)

    if args.output:
        with open(args.output, "w") as f:
            json.dump(results, f, indent=2)

    sys.exit(1 if invalid else 0)


if __name__ == "__main__":
    Main()
