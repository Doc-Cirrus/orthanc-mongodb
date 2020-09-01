import math
from datetime import datetime
from multiprocessing.pool import Pool

import json
from pydash import pick, py_, times
from pymongo import MongoClient, UpdateOne
from pymongo.errors import BulkWriteError

PAGE_LIMIT = 2000


def _setup_arguments():
    """Setup the command line arguments"""
    # Description
    parser = argparse.ArgumentParser(
        description="FireThunder package generator",
        usage="%(prog)s DATABASE_CONNECTION_URL"
    )

    # Parameters
    parser.add_argument(
        "DATABASE_URL", type=str,
        help="path to configuration file as json format"
    )

    return parser.parse_args()


def chunk_handler(level, page, DATABASE_URL):
    def chunk_handler(level, page):
    db_client = MongoClient(DATABASE_URL)
    db_instance = db_client.get_default_database()
    collection_instance = db_instance.get_collection('Resources')

    query = [
        {"$match": {"resourceType": level, "0": {"$exists": False}}},
        {"$skip": (page - 1) * PAGE_LIMIT},
        {"$limit": PAGE_LIMIT},
        {
            "$addFields": {
                "level": "$$ROOT"
            }
        },
        {
            "$graphLookup": {
                "from": "Resources",
                "startWith": "$internalId",
                "connectFromField": "internalId",
                "connectToField": "parentId",
                "as": "children"
            }
        },
        {
            "$graphLookup": {
                "from": "Resources",
                "startWith": "$parentId",
                "connectFromField": "parentId",
                "connectToField": "internalId",
                "as": "parents"
            }
        },
        {
            "$addFields": {
                "resources": {
                    "$concatArrays": [
                        ["$level"],
                        {
                            "$ifNull": [
                                "$children", []
                            ]
                        },
                        {
                            "$ifNull": [
                                "$parents", []
                            ]
                        }
                    ]
                }
            }
        },
        {
            "$project": {
                "_id": 1,
                "internalId": 1,
                "0": {
                    "$filter": {
                        "input": "$resources",
                        "as": "item",
                        "cond": {"$eq": ["$$item.resourceType", 0]}
                    }
                },
                "1": {
                    "$filter": {
                        "input": "$resources",
                        "as": "item",
                        "cond": {"$eq": ["$$item.resourceType", 1]}
                    }
                },
                "2": {
                    "$filter": {
                        "input": "$resources",
                        "as": "item",
                        "cond": {"$eq": ["$$item.resourceType", 2]}
                    }
                },
                "3": {
                    "$filter": {
                        "input": "$resources",
                        "as": "item",
                        "cond": {"$eq": ["$$item.resourceType", 3]}
                    }
                }
            }
        }
    ]

    if level == 1 or level == 2:
        query.append({
            "$lookup": {
                "as": "sorts",
                "from": "MainDicomTags",
                "let": {
                    "resource": "$internalId"
                },
                "pipeline": [
                    {
                        "$match": {
                            "$expr": {
                                "$eq": ["$id", "$$resource"]
                            },
                            "$or": [
                                {
                                    "tagGroup": 8,
                                    "tagElement": 32 if level == 1 else 33
                                },
                                {
                                    "tagGroup": 8,
                                    "tagElement": 48 if level == 1 else 49
                                }
                            ]
                        }
                    }
                ]
            }
        })

    requests = []
    for item in collection_instance.aggregate(pipeline=query):
        update_dict = assign(
            {
                '0': py_(item).get('0').map('internalId').value(),
                '1': py_(item).get('1').map('internalId').value(),
                '2': py_(item).get('2').map('internalId').value(),
                '3': py_(item).get('3').map('internalId').value(),

                'instancePublicId': py_(item).get('3').head().get('publicId').value()
            },
            {
                "sorts": py_(item).get('sorts').map('value').value()
            } if level == 1 or level == 2 else {}
        )

        requests.append(
            UpdateOne(pick(item, '_id'), {'$set': update_dict})
        )

    try:
        print(f"==============={level}-{page}-{len(requests)}===============")
        r = collection_instance.bulk_write(requests)
        print(json.dumps(r.bulk_api_result, indent=4))
        print(f"======================================")

    except BulkWriteError as bwe:
        print(json.dumps(bwe.details, indent=4))


def handle_chunk(DATABASE_URL):
    client_ = MongoClient(DATABASE_URL)
    db_ = client_.get_default_database()
    collection_ = db_.get_collection('Resources')

    patients_count = collection_.count_documents({"resourceType": 0, "0": {"$exists": False}})
    study_count = collection_.count_documents({"resourceType": 1, "0": {"$exists": False}})
    series_count = collection_.count_documents({"resourceType": 2, "0": {"$exists": False}})
    instance_count = collection_.count_documents({"resourceType": 3, "0": {"$exists": False}})

    patients_pages = math.ceil(patients_count / PAGE_LIMIT)
    study_pages = math.ceil(study_count / PAGE_LIMIT)
    series_pages = math.ceil(series_count / PAGE_LIMIT)
    instance_pages = math.ceil(instance_count / PAGE_LIMIT)

    processes_args = py_([]).concat(
        times(patients_pages, lambda i: (0, i + 1, DATABASE_URL)),
        times(study_pages, lambda i: (1, i + 1, DATABASE_URL)),
        times(series_pages, lambda i: (2, i + 1, DATABASE_URL)),
        times(instance_pages, lambda i: (3, i + 1, DATABASE_URL)),
    ).chunk(4).value()

    for chunk in processes_args:
        with Pool(processes=4) as pool:
            for portion in chunk:
                pool.apply_async(chunk_handler, args=portion)

            pool.close()
            pool.join()


if __name__ == "__main__":
    args = _setup_arguments()
    DATABASE_URL = args.DATABASE_URL
    
    print("now =", datetime.now())

    client = MongoClient(DATABASE_URL)
    db = client.get_default_database()
    collection = db.get_collection('Resources')

    while True:
        count = collection.count_documents({"0": {"$exists": False}})
        if count > 0:
            handle_chunk(DATABASE_URL)
        else:
            break

    print("now =", datetime.now())
