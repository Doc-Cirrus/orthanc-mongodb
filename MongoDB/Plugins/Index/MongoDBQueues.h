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

#include <string>


namespace OrthancDatabases
{
  /**
   * The "Queues" collection { queueId, id, value, reservedUntil }. The
   * "id" comes from the sequence "Queues" and orders each queue. The
   * optional "reservedUntil" is a date of the server clock, so that
   * several Orthanc agree on when a reservation expires. Each pop and
   * reservation is a single atomic "findAndModify".
   **/
  class MongoDBQueues : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

  public:
    explicit MongoDBQueues(DatabaseManager& manager);

    void EnqueueValue(const std::string& queueId,
                      const std::string& value);

    // The values reserved by "ReserveQueueValue()" are skipped until their reservation expires
    bool DequeueValue(std::string& value,
                      const std::string& queueId,
                      bool fromFront);

    // Includes the reserved values
    uint64_t GetQueueSize(const std::string& queueId);

    bool ReserveQueueValue(std::string& value,
                           uint64_t& valueId,
                           const std::string& queueId,
                           bool fromFront,
                           uint32_t reserveTimeout /* seconds */);

    // Throws "UnknownResource" if the reservation has expired
    void AcknowledgeQueueValue(const std::string& queueId,
                               uint64_t valueId);
  };
}
