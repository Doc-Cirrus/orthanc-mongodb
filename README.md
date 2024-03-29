<!-- @format -->

# MongoDB database plugins for Orthanc DICOM Server

## Overview

The repository contains two plugins to store the data in MongoDB database.
This was based on [orthanc-database](https://hg.orthanc-server.com/orthanc-databases/) and supporting the latest database API (orthanc@1.9.2+).
We've comprehensively refactored the codebase, eliminating all SQL-dependent components and replacing them with their MongoDB counterparts. 
Additionally, we've extended the existing build toolchain to seamlessly integrate with MongoDB, ensuring a smooth transition to this new data storage solution.

## Notes

- **This is still a work in progress (UnitTests, etc...)** check todos
- If you skipped the [1.9.1](https://github.com/Doc-Cirrus/orthanc-mongodb/tree/1.9.1) tag, the migration section there
  will still need to executed, unless this is a fresh installation.

## Documentation

For complete documentation check [Docs](./docs/README.md).

## Todos

- Add unit tests and extend them.
- Add coverage.
