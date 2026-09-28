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


#pragma once

#include "../../../Framework/Common/DatabaseManager.h"
#include "../../../Framework/MongoDB/MongoDBDatabase.h"

#include <list>


namespace OrthancDatabases
{
  /**
   * The "PatientRecyclingOrder" collection { id, patientId } lists
   * the unprotected patients, "id" (from the sequence of the same
   * name) giving the order of recycling. Protected patients have no
   * entry.
   **/
  class MongoDBPatientRecycling : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

    bool SelectPatientToRecycle(int64_t& internalId,
                                const bsoncxx::document::view_or_value& filter);

  public:
    explicit MongoDBPatientRecycling(DatabaseManager& manager);

    // Counterpart of the "PatientAdded" trigger of the SQL plugins
    void AddPatient(int64_t patient);

    bool SelectPatientToRecycle(int64_t& internalId /*out*/);

    bool SelectPatientToRecycle(int64_t& internalId /*out*/,
                                int64_t patientIdToAvoid);

    bool IsProtectedPatient(int64_t internalId);

    void SetProtectedPatient(int64_t internalId,
                             bool isProtected);

    void TagMostRecentPatient(int64_t patient);

    uint64_t GetUnprotectedPatientsCount();

    void DeleteForPatients(const std::list<int64_t>& patients);
  };
}
