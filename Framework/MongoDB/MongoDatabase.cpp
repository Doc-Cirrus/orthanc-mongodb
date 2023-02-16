/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2023  (Doc Cirrus GmbH)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/


#include "MongoDatabase.h"
#include "../Common/ImplicitTransaction.h"
#include "../Common/ITransaction.h"

#include <OrthancException.h>

namespace OrthancDatabases {
    IPrecompiledStatement *MongoDatabase::Compile(const Query &query) {
        return nullptr;
    }

    namespace {
        class DummyTransaction : public ITransaction {

        public:
            explicit DummyTransaction() {}

            virtual bool IsImplicit() const ORTHANC_OVERRIDE {
                return false;
            }

            virtual void Rollback() override {
            }

            virtual void Commit() override {
            }

            virtual IResult *Execute(IPrecompiledStatement &statement,
                                     const Dictionary &parameters) override {
                return nullptr;
            }

            virtual void ExecuteWithoutResult(IPrecompiledStatement &statement,
                                              const Dictionary &parameters) override {}

            virtual bool DoesTableExist(const std::string &name) override {
                return true;
            }

            virtual bool DoesTriggerExist(const std::string &name) override {
                return false;
            }

            virtual void ExecuteMultiLines(const std::string &query) override {
            }
        };
    }

    ITransaction *MongoDatabase::CreateTransaction(TransactionType type) {
        return new DummyTransaction();
    }

    // factory related
    class MongoDatabase::Factory : public IDatabaseFactory {
    private:
        std::string url_;
        int chunkSize_;

    public:
        Factory(const std::string &url, const int &chunkSize) : url_(url), chunkSize_(chunkSize) {}

        virtual IDatabase *Open() override {
            std::unique_ptr<MongoDatabase> db(new MongoDatabase);
            db->SetChunkSize(chunkSize_);
            db->Open(url_);

            return db.release();
        }
    };

    IDatabaseFactory *MongoDatabase::CreateDatabaseFactory(const std::string &url, const int &chunkSize) {
        return new Factory(url, chunkSize);
    }

    MongoDatabase *MongoDatabase::CreateDatabaseConnection(const std::string &url, const int &chunkSize) {
        Factory factory(url, chunkSize);
        return dynamic_cast<MongoDatabase *>(factory.Open());
    }
}
