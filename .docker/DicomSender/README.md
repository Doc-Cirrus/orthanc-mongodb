# DICOM sender and sample studies

Tools to fill an Orthanc with DICOM studies of many modalities, for tests, load tests and demonstrations:

- `fetch-samples.py` downloads the open-source sample studies listed in `dicom-sources.json` and checks them;
- `dicom-sender.py` sends DICOM files to a PACS by C-STORE, as new studies of new patients, as many times as needed;
- `seed.py` does both, on the first `docker compose up` of the runtime environment only (the `dicom-sender` service of `../docker-compose.override.runtime.yaml.example`).

No DICOM file is kept in this repository or in the image: the samples are downloaded when needed.

## Prerequisites

- Python 3.9 or later, with the modules of `requirements.txt`:
  ```bash
  pip install -r requirements.txt
  ```
- Or Docker: the `dicom-sender` service of the runtime environment runs the scripts in the image built from the `Dockerfile` of this folder.

## Sample studies

`dicom-sources.json` lists 12 samples: 1,497 instances, about 650 MB, in 11 modalities.

| Identifier | Modalities | Instances | Size | Content |
|---|---|---:|---:|---|
| `tcia-acrin-nsclc-fdg-pet-ct-pt` | CT, PT | 309 | 62.5 MB | Lung PET-CT study: CT and two PET reconstructions |
| `tcia-eay131-large-ct` | CT | 945 | 500 MB | Large CT study: 889 thin axial slices (0.625 mm), with sagittal and coronal reconstructions |
| `tcia-cmb-mml-brain-mr` | MR | 220 | 31.5 MB | Brain MR study with seven series |
| `tcia-cmb-lca-nm` | NM | 1 | 0.3 MB | Lung ventilation scintigraphy |
| `tcia-covid-19-ar-cr` | CR | 1 | 5.6 MB | Chest radiograph |
| `tcia-lidc-idri-dx` | DX | 1 | 6.9 MB | Chest radiograph |
| `tcia-cmmd-mg` | MG | 2 | 8.8 MB | Mammography, two views |
| `tcia-cmb-brca-us` | US | 10 | 9.5 MB | Ultrasound-guided liver biopsy |
| `tcia-cmb-aml-xa` | XA | 1 | 2.1 MB | Angiography |
| `tcia-remind-us-multiframe` | US | 1 | 16.9 MB | Ultrasound volume, one multi-frame object of 39 frames |
| `tcia-varepop-apollo-rf` | RF | 1 | 2.4 MB | Barium fluoroscopy |
| `pydicom-compressed` | MR, NM, OT | 5 | < 0.1 MB | Small images in JPEG baseline, JPEG lossless, JPEG 2000, RLE and Deflate |

The TCIA samples are uncompressed (Explicit or Implicit VR Little Endian). The compressed transfer syntaxes come from `pydicom-compressed`. Orthanc stores JPEG 2000 as it is, but needs the [GDCM plugin](https://orthanc.uclouvain.be/book/plugins/gdcm.html) to render its previews.

### Licences

The samples are published under open licences, and are only downloaded, never redistributed by this repository:

- **TCIA** ([The Cancer Imaging Archive](https://www.cancerimagingarchive.net/)): each collection is under the Creative Commons Attribution license, 3.0 or 4.0, given by `license` and `licenseUrl` in `dicom-sources.json`. When you publish results obtained with these data, cite the collection given by `citation` (its DOI). The data are de-identified by TCIA (`PatientIdentityRemoved` is `YES` in every file), and each download contains the `LICENSE` of its collection.
- **pydicom**: test files of the pydicom 2.4.4 repository, under the MIT license (the files taken from GDCM are under the BSD 3-Clause license). They are synthetic or edited test images, with fictional patients.

The three sources suggested at first were left out, as none of them has an open licence:

- [Siemens MAGNETOM World](https://www.magnetomworld.siemens-healthineers.com/clinical-corner/protocols/dicom-images): its terms of use forbid copying and distributing the content.
- [DICOM Library](https://www.dicomlibrary.com/): its terms allow personal use only, and the files are uploaded by its users.
- [Rubo Medical](https://www.rubomedical.com/dicom_files/): the files come with no licence: they "can be used for evaluation of the 'Rubo DICOM Viewer 2.0'".

Enhanced MR (multi-frame MR) and JPEG-LS have no sample yet: TCIA serves its MR as single frames, and the JPEG-LS test file of pydicom has the same SOP Instance UID as its RLE one.

## `fetch-samples.py`

Downloads the samples into `$SAMPLES_DIR` (default: `~/.cache/orthanc-mongodb/samples`), and unpacks each one into `<cache>/dicom/<identifier>/`, the default input of `dicom-sender.py`. A sample already there and complete is not downloaded again.

```bash
./fetch-samples.py --list                        # list the samples, download nothing
./fetch-samples.py                               # download all of them (about 650 MB)
./fetch-samples.py --modality CT,MR              # only the samples with CT or MR
./fetch-samples.py --source tcia-cmmd-mg,pydicom-compressed
./fetch-samples.py --force                       # download again the samples already there
```

| Option | Default | Description |
|---|---|---|
| `--manifest` | `dicom-sources.json` next to the script | List of the samples. |
| `--cache` | `$SAMPLES_DIR`, or `~/.cache/orthanc-mongodb/samples` | Download folder. |
| `--source` | all | Comma-separated identifiers of the samples. |
| `--modality` | all | Comma-separated modalities: only the samples that contain one of them. |
| `--list` | | List the samples, and download nothing. |
| `--force` | | Download again the samples that are already there. |
| `--describe` | | Compute the fields of the manifest from the downloaded files, and write them into it (see below). |

Each sample is checked after its download: the SHA-256 of its DICOM files must equal `dicomSha256` in the manifest. If a sample fails, the others are still downloaded, and the script exits with status 1.

## `dicom-sender.py`

Sends all the DICOM files of a folder by C-STORE, `--copies` times, with `--processes` parallel associations. For each copy, every study gets new Study and Series Instance UIDs and a new random patient (ID, name, birth date, sex, study ID and date), so the copies are distinct studies with the same images. The SOP Instance UIDs are kept, unless `--new-sop-uids` is given.

The files are sent as they are, in their own transfer syntax: the script proposes one presentation context per SOP class and transfer syntax found in the input. DICOMDIR and non-DICOM files are skipped.

| Option | Default | Description |
|---|---|---|
| `--input` | `$SAMPLES_DIR/dicom` | Folder of the DICOM files, read recursively. |
| `--source` | all | Comma-separated subfolders of the input to send, e.g. identifiers of samples. |
| `--host`, `--port`, `--aet` | `localhost`, `4242`, `ORTHANC` | The PACS. |
| `--calling-aet` | `DICOM_SENDER` | AE title of the script. |
| `--copies` | `4` | Number of times the input is sent. |
| `--processes` | `2` | Number of parallel associations. Each copy is sent over one association, so use at least as many copies as processes. |
| `--new-sop-uids` | off | Give new SOP Instance UIDs to each copy. Without it, the copies share their SOP Instance UIDs. |
| `--timeout` | `60` | Network timeout, in seconds. |
| `--quiet` | off | Only print the summary, not one line per file. |
| `--report` | | Write the summary and one row per C-STORE (file, size, latency, status) to this JSON file. |

At the end, it prints the number of stored files, the instances and megabytes per second, and the median and 95th-percentile store latency. It exits with status 1 if any file was not stored.

Examples:

```bash
# All the samples once, to the local Orthanc of the runtime environment
./dicom-sender.py --copies 1

# A load test: 100 copies of the CT and MR studies (52,900 instances), 8 associations, with a report
./dicom-sender.py --source tcia-acrin-nsclc-fdg-pet-ct-pt,tcia-cmb-mml-brain-mr \
                  --copies 100 --processes 8 --quiet --report load-test.json

# Any other folder of DICOM files
./dicom-sender.py --input /data/dicom --host pacs.example.com --port 104 --aet PACS
```

## Seeding the runtime environment

On the first `docker compose up` of the runtime environment, the `dicom-sender` service downloads the samples into the volume `orthanc-mongodb-samples`, waits until Orthanc is healthy, and sends them:

```bash
cd .docker
cp docker-compose.override.runtime.yaml.example docker-compose.override.yaml
docker compose up -d
docker compose logs -f dicom-sender
```

Once the samples are stored, it writes the marker `.seeded` in the same volume. Compose starts the service again on each `docker compose up`, as it does for every stopped container, but the service then finds the marker and exits at once: nothing is downloaded or sent again. If the seed fails (e.g. Orthanc unreachable), no marker is written, and the next `docker compose up` tries again.

To seed again, e.g. after deleting the MongoDB volume `orthanc-mongodb-data`, ignore the marker:

```bash
docker compose run --rm dicom-sender python3 seed.py --force
```

Even then, nothing is sent if Orthanc already stores at least as many instances as the seed, so a seed is never added twice. Deleting the volume `orthanc-mongodb-samples` also resets the marker, and the samples are downloaded again.

These settings of `.env` choose what is sent:

| Variable | Default | Description |
|---|---|---|
| `SEED_ENABLED` | `true` | `false` never seeds. |
| `SEED_SOURCES` | all | Comma-separated identifiers of the samples, e.g. all but the 500 MB CT study. |
| `SEED_COPIES` | `1` | Number of copies of the samples. |
| `SEED_PROCESSES` | `2` | Number of parallel associations. |

The scripts can also be run in the image, e.g. `docker compose run --rm dicom-sender python3 dicom-sender.py --host orthanc --copies 10`.

## `dicom-sources.json`

An array with one object per sample:

| Field | Description |
|---|---|
| `id` | Identifier, also the name of the folder of the sample. |
| `title`, `notes` | Description. |
| `source`, `page` | Where the sample comes from. |
| `license`, `licenseUrl`, `citation` | Licence of the sample, and how to cite it. |
| `urls` | Downloads: ZIP archives or single DICOM files. |
| `dicomSha256` | SHA-256 of the sorted list of the SHA-256 of the DICOM files, one hexadecimal digest per line. The DICOM files are checked rather than the archives, as TCIA generates its archives on each request. |
| `size` | Total size of the DICOM files, in bytes. |
| `modalities`, `sopClasses`, `transferSyntaxes` | What the DICOM files contain. |
| `studies`, `series`, `instances` | Counts of the DICOM files. Files with the same SOP Instance UID count as one instance, as in Orthanc. |
| `deidentified` | Whether every file has `PatientIdentityRemoved` set to `YES`. |
| `fetched` | Date of the download that computed these fields. |

To add a sample:

1. Check that its licence allows this use, and that it holds no real patient data (or is de-identified).
2. Add an object with `id`, `title`, `source`, `page`, `license`, `licenseUrl`, `citation` and `urls`. For a TCIA series, the URL is `https://services.cancerimagingarchive.net/nbia-api/services/v1/getImage?SeriesInstanceUID=<uid>`.
3. Run `./fetch-samples.py --describe --source <id>`: it downloads the sample and writes the other fields into the manifest.
