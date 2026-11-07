/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
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


#include "MongoDBPatientRecycling.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  static const char* const COLLECTION = "PatientRecyclingOrder";


  MongoDBPatientRecycling::MongoDBPatientRecycling(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBPatientRecycling::AddPatient(int64_t patient)
  {
    const int64_t seq = database_.GetNextSequence(COLLECTION);
    database_.GetCollection(COLLECTION).InsertOne(make_document(kvp("id", seq), kvp("patientId", patient)));
  }


  bool MongoDBPatientRecycling::SelectPatientToRecycle(int64_t& internalId,
                                                       const bsoncxx::document::view_or_value& filter)
  {
    // The oldest entry comes first
    mongocxx::options::find options;
    options.sort(make_document(kvp("id", 1)));

    std::optional<bsoncxx::document::value> result = database_.GetCollection(COLLECTION).FindOne(filter, options);

    if (result)
    {
      internalId = MongoDBToolbox::GetInteger(result->view(), "patientId");
      return true;
    }
    else
    {
      return false;
    }
  }


  bool MongoDBPatientRecycling::SelectPatientToRecycle(int64_t& internalId /*out*/)
  {
    return SelectPatientToRecycle(internalId, make_document());
  }


  bool MongoDBPatientRecycling::SelectPatientToRecycle(int64_t& internalId /*out*/,
                                                       int64_t patientIdToAvoid)
  {
    return SelectPatientToRecycle(internalId, make_document(
                                    kvp("patientId", make_document(kvp("$ne", patientIdToAvoid)))));
  }


  bool MongoDBPatientRecycling::IsProtectedPatient(int64_t internalId)
  {
    return !database_.GetCollection(COLLECTION).Exists(make_document(kvp("patientId", internalId)));
  }


  void MongoDBPatientRecycling::SetProtectedPatient(int64_t internalId,
                                                    bool isProtected)
  {
    if (isProtected)
    {
      database_.GetCollection(COLLECTION).DeleteMany(make_document(kvp("patientId", internalId)));
    }
    else if (IsProtectedPatient(internalId))
    {
      AddPatient(internalId);
    }
    else
    {
      // Nothing to do: The patient is already unprotected
    }
  }


  void MongoDBPatientRecycling::TagMostRecentPatient(int64_t patient)
  {
    // Moves the patient to the end of the recycling order, if unprotected
    const int64_t seq = database_.GetNextSequence(COLLECTION);

    database_.GetCollection(COLLECTION).UpdateOne(
      make_document(kvp("patientId", patient)),
      make_document(kvp("$set", make_document(kvp("id", seq)))));
  }


  uint64_t MongoDBPatientRecycling::GetUnprotectedPatientsCount()
  {
    return static_cast<uint64_t>(database_.GetCollection(COLLECTION).CountDocuments(make_document()));
  }


  void MongoDBPatientRecycling::DeleteForPatients(const std::list<int64_t>& patients)
  {
    database_.GetCollection(COLLECTION).DeleteMany(
      make_document(kvp("patientId", make_document(kvp("$in", MongoDBToolbox::ToArray(patients))))));
  }
}
