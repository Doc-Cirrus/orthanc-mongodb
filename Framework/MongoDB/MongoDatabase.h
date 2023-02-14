#pragma once

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/pool.hpp>

#include "../Common/IDatabase.h"
#include "../Common/IDatabaseFactory.h"
#include <Logging.h>

// mongocxx related
using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

namespace OrthancDatabases {
    static mongocxx::instance &inst = mongocxx::instance::current();

    class MongoDatabase : public IDatabase {
    private:
        class Factory;

        // std::mutex mutex_;
        int chunkSize_;
        std::string dbname_;
        std::unique_ptr<mongocxx::pool> pool_;

    public:
        void Open(const std::string &url) {
            auto const uri = mongocxx::uri{url};

            dbname_ = uri.database();
            mongocxx::pool *p = new mongocxx::pool(uri);

            SetPool(p);
        }

        void SetChunkSize(const int &chunkSize) {
            chunkSize_ = chunkSize;
        }

        void SetPool(mongocxx::pool *pool) {
            pool_.reset(pool);
        }

        mongocxx::pool &GetPool() const {
            return *pool_;
        }

        mongocxx::pool::entry GetPoolEntry() const {
            return GetPool().acquire();
        }

        mongocxx::database GetObject() const {
            auto entry = GetPoolEntry();
            return (*entry)[dbname_];
        }

        mongocxx::collection GetCollection(const std::string &name) const {
            auto database = GetObject();
            return database[name];
        }

        mongocxx::collection GetCollection(const mongocxx::database &database, const std::string &name) const {
            return database[name];
        }

        // database related tasks
        bool IsMaster() const {
            auto database = GetObject();
            auto isMasterDocument = database.run_command(make_document(kvp("isMaster", 1)));

            return isMasterDocument.view()["ismaster"].get_bool().value;
        }

        void CreateIndices() {
            auto database = GetObject();

            GetCollection(database, "fs.files").create_index(make_document(kvp("filename", 1)));

            GetCollection(database, "Resources").create_index(make_document(kvp("parentId", 1)));
            GetCollection(database, "Resources").create_index(make_document(kvp("publicId", 1)));
            GetCollection(database, "Resources").create_index(make_document(kvp("resourceType", 1)));
            GetCollection(database, "Resources").create_index(make_document(kvp("internalId", 1)));
            GetCollection(database, "PatientRecyclingOrder").create_index(make_document(kvp("patientId", 1)));
            GetCollection(database, "MainDicomTags").create_index(make_document(kvp("id", 1)));
            GetCollection(database, "MainDicomTags").create_index(
                    make_document(kvp("tagGroup", 1), kvp("tagElement", 1), kvp("value", 1))
            );
            GetCollection(database, "DicomIdentifiers").create_index(make_document(kvp("id", 1)));
            GetCollection(database, "DicomIdentifiers").create_index(
                    make_document(kvp("tagGroup", 1), kvp("tagElement", 1), kvp("value", 1))
            );
            GetCollection(database, "Changes").create_index(make_document(kvp("internalId", 1)));
            GetCollection(database, "AttachedFiles").create_index(make_document(kvp("id", 1)));
            GetCollection(database, "GlobalProperties").create_index(make_document(kvp("property", 1)));
            GetCollection(database, "ServerProperties").create_index(
                    make_document(kvp("server", 1), kvp("property", 1))
            );
        }

        int64_t GetNextSequence(const std::string &sequence) const {
            // std::lock_guard<std::mutex> lock(mutex_);

            int64_t num = 1;
            auto collection = GetCollection("Sequences");

            mongocxx::options::find_one_and_update options;
            options.return_document(mongocxx::options::return_document::k_after);
            auto seqDocument = collection.find_one_and_update(
                    make_document(kvp("name", sequence)),
                    make_document(kvp("$inc", make_document(kvp("i", int64_t(1))))),
                    options
            );

            if (seqDocument) {
                bsoncxx::document::element element = seqDocument->view()["i"];
                num = element.get_int64().value;
            } else {
                collection.insert_one(make_document(
                        kvp("name", sequence),
                        kvp("i", int64_t(1))
                ));
            }

            return num;
        }

        virtual Dialect GetDialect() const override {
            return Dialect_SQLite;
        }

        virtual IPrecompiledStatement *Compile(const Query &query) override;
        virtual ITransaction *CreateTransaction(TransactionType type) override;

        static IDatabaseFactory* CreateDatabaseFactory(const std::string &url, const int &chunkSize);
        static MongoDatabase* CreateDatabaseConnection(const std::string &url, const int &chunkSize);
    };
}
