# Orthanc MongoDB testing

## Brief test strategy

The Orthanc MongoDB plugin allows storing DICOM data in MongoDB.
It's only data storage/retrieval level and testing for the plugin is focused on this part.
Whole DICOM Orthanc functionality is out of the scope of the MongoDB plugin testing.

### Test levels

The plugin is tests on unit, integration and system level.

* Unit tests are implemented for all methods of the IndexPlugin as well as for StoragePlugin
* In the test strategy for the Orthanc MongoDB plugin the integration and system testing are combined together and are covered by:
    * Manual functional tests
    * Command line scripts for loading DCM files (load test)

### Environment requirements

The plugin is being tested on Linux (primary), OSX and windows platforms.
Configured Orthanc instance is required with the Index and Storage plugins activated. Please see the [Plugin configuration setup section in README.md](README.md)
DICOM sample images are required for testing.

### Testing tools

Manual tests are performed in web browser.
Load tests are done with the [dcm4che tools 3](https://dcm4che.atlassian.net/wiki/display/lib/)

## Manual Test Scenarios

1. Upload test
    * Open Ortanc home -> got to upload and upload Test DCM file
    * Refresh page, ensure the Patient/Study/Series/Instance information is correct
    * Verify DICOM tags
    * Preview instance image
    * Expected results: DICOM tags are correct, image in preview appears correct.
2. Delete test
    * Upload patient
    * Refresh page
    * Delete patient
    * Refresh page
    * Expected results: patient is deleted with all studies/series/instances hierarchy. There are no records regarding patient in DB.
3. Protect-unprotect
    * Upload patient
    * Click on protect/un-protect option
    * Refresh page
    * Expected results: patient protected remains consistent.
4. Send to remote modality
    * Configure second Orthanc instance to accept DICOM images. ("DicomModalities" section of the config file.)
    * Upload patient
    * Send patient to remote modality
    * Verify the patient on remote Orthanc instance
    * Expected results: patient on remote Orthanc instance is the same.
5. Query/Retrieve from remote modality
    * Ensure there are patients in the remote instance
    * On the main instance go to the Query/Retrieve
    * Search patients in remote modality, retrieve to local
    * Expected results: patient search returns correct results, patients/studies/series correctly retrieved to local.

## Load test scripts

Download sample test data from [here](https://wiki.cancerimagingarchive.net/display/Public/LIDC-IDRI#b261a131fc93463d83fd3dd09fd0edf6)
or any other source.

* Use storescu utility to upload samples:

```
storescu -c ORTHANC@orthank-host:4242 Folder_with_samples
```
* Generation

It's possible to generate similar set from Folder_with_samples like that:

```
find Folder_with_samples -name '*.dcm' | xargs -L 1 -I {} dcmgen 1 {} generated_folder
```

* Generate bunch of patients from existing seed data. The script generate 50000 unique patients with 4 series 5 instances each.

```
#!/bin/bash
GEN_FOLDER=generated

for i in {1..50000}
do
   echo "Generating: $i"
   dcmgen 20:4 seed_small.dcm $GEN_FOLDER --override PatientID=`uuidgen` --override PatientName=`uuidgen`
done
```
seed_small.dcm - is a single file to take sample data from and generate several DICOM tags.

Then upload generated sources with storescu

* Delete all patients
```
curl http://localhost:8042/patients | grep -o "[0-9a-z\-]*" | grep -v "^$" | xargs -I {} curl -X "DELETE" http://localhost:8042/patients/{}
```

Selectively verify uploaded data in UI.
Check Orthanc server logs for errors.
Check MongoDB server logs for errors.

## Test results

### Unit test results
```
[==========] Running 9 tests from 2 test cases.
[----------] Global test environment set-up.
[----------] 8 tests from MongoDBBackendTest
[ RUN      ] MongoDBBackendTest.Attachments
[       OK ] MongoDBBackendTest.Attachments (700 ms)
[ RUN      ] MongoDBBackendTest.Resource
[       OK ] MongoDBBackendTest.Resource (657 ms)
[ RUN      ] MongoDBBackendTest.Changes
[       OK ] MongoDBBackendTest.Changes (774 ms)
[ RUN      ] MongoDBBackendTest.ExportedResources
[       OK ] MongoDBBackendTest.ExportedResources (679 ms)
[ RUN      ] MongoDBBackendTest.Metadata
[       OK ] MongoDBBackendTest.Metadata (669 ms)
[ RUN      ] MongoDBBackendTest.GlobalProperty
[       OK ] MongoDBBackendTest.GlobalProperty (643 ms)
[ RUN      ] MongoDBBackendTest.ProtectedPatient
[       OK ] MongoDBBackendTest.ProtectedPatient (681 ms)
[ RUN      ] MongoDBBackendTest.MainDicomTags
[       OK ] MongoDBBackendTest.MainDicomTags (694 ms)
[----------] 8 tests from MongoDBBackendTest (5497 ms total)

[----------] 1 test from ConfigurationTest
[ RUN      ] ConfigurationTest.Configuration
[       OK ] ConfigurationTest.Configuration (0 ms)
[----------] 1 test from ConfigurationTest (0 ms total)

[----------] Global test environment tear-down
[==========] 9 tests from 2 test cases ran. (5497 ms total)
[  PASSED  ] 9 tests.
```
```
[==========] Running 1 test from 1 test case.
[----------] Global test environment set-up.
[----------] 1 test from MongoDBStorageTest
[ RUN      ] MongoDBStorageTest.StoreFiles
[       OK ] MongoDBStorageTest.StoreFiles (134 ms)
[----------] 1 test from MongoDBStorageTest (134 ms total)

[----------] Global test environment tear-down
[==========] 1 test from 1 test case ran. (135 ms total)
[  PASSED  ] 1 test.
```

### Manual test results

Test | Linux | Windows | OSX
--- | --- | --- | ---
1. Upload test | ![OK](img/green_tick.png) | ![OK](img/green_tick.png) | ![OK](img/green_tick.png)
2. Delete test | ![OK](img/green_tick.png) | ![OK](img/green_tick.png) | ![OK](img/green_tick.png)
3. Protect-unprotect | ![OK](img/green_tick.png) | ![OK](img/green_tick.png) | ![OK](img/green_tick.png)
4. Send to remote modality | ![OK](img/green_tick.png) | ![OK](img/green_tick.png) | ![OK](img/green_tick.png)
5. Query/Retrieve from remote modality | ![OK](img/green_tick.png) | ![OK](img/green_tick.png) | ![OK](img/green_tick.png)


### Load test results:

There were couple of load cycles, of course all depends on the network/hardware setup.
Tests were made on i3 (2 physical/4 logical cores) and HDD (magnetic).
Loading to localhost, samples were on a separate HDD, MongoDB database on own HDD.

```
Sent 244,255 objects (=127,645.219MB) in 10,871.803s (=11.741MB/s) - avg sample size: 0.5Mb
Sent 732,765 objects (=382,892.125MB) in 35,922.105s (=10.659MB/s) - avg sample size: 0.5Mb
Sent 240,000 objects (=342,417.25MB) in 18,605.527s (=18.404MB/s) - avg sample size: 1.43Mb
```

