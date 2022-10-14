## Overview

If you're upgrading from 1.8.2 version of the plugin. You'll need to execute a migration because this version (1.9.1) has some schema changes in orther to to optimize queries.

## Prepation

The migration script is a python3 one. which require python binnary and pip to be installed in the system [how to](https://realpython.com/installing-python/).
Then well need to install the dependencies, it's recomended to do that in virtual env:

```bash
cd Migration
python -m venv .migration
pip install -r requirement.txt
```

## Execution

The migration script only requires the database connection string, here is an example of the cmd

```
python script.py mongodb://localhost:27017/orthanc_db
```
The script execution time depends on the size of the database, but usually won't take more than 10min.