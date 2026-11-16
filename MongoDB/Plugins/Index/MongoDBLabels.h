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
#include <string>


namespace OrthancDatabases
{
  /**
   * The "Labels" collection { id, label }, with one document per label
   * of a resource (new in schema revision 2). The unique index on
   * (id, label) makes "AddLabel()" idempotent.
   **/
  class MongoDBLabels : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

  public:
    static const char* const COLLECTION;

    explicit MongoDBLabels(DatabaseManager& manager);

    void AddLabel(int64_t resource,
                  const std::string& label);

    void RemoveLabel(int64_t resource,
                     const std::string& label);

    void ListLabels(std::list<std::string>& target,
                    int64_t resource);

    void ListAllLabels(std::list<std::string>& target);

    void DeleteForResources(const std::list<int64_t>& resources);
  };
}
