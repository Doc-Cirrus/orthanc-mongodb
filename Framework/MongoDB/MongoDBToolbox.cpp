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


#include "MongoDBToolbox.h"

#include <OrthancException.h>


namespace OrthancDatabases
{
  namespace MongoDBToolbox
  {
    static Orthanc::OrthancException BadField(const std::string& key)
    {
      return Orthanc::OrthancException(Orthanc::ErrorCode_Database,
                                       "Missing or badly typed field in a MongoDB document: " + key);
    }


    bool LookupInteger(int64_t& target,
                       const bsoncxx::document::view& document,
                       const std::string& key)
    {
      bsoncxx::document::element element = document[key];

      if (!element)
      {
        return false;
      }

      switch (element.type())
      {
        case bsoncxx::type::k_int64:
          target = element.get_int64().value;
          return true;

        case bsoncxx::type::k_int32:
          target = element.get_int32().value;
          return true;

        case bsoncxx::type::k_null:
          return false;

        default:
          throw BadField(key);
      }
    }


    int64_t GetInteger(const bsoncxx::document::view& document,
                       const std::string& key)
    {
      int64_t value;
      if (LookupInteger(value, document, key))
      {
        return value;
      }
      else
      {
        throw BadField(key);
      }
    }


    int32_t GetInt32(const bsoncxx::document::view& document,
                     const std::string& key)
    {
      const int64_t value = GetInteger(document, key);

      if (value < INT32_MIN || value > INT32_MAX)
      {
        throw BadField(key);
      }

      return static_cast<int32_t>(value);
    }


    bool LookupString(std::string& target,
                      const bsoncxx::document::view& document,
                      const std::string& key)
    {
      bsoncxx::document::element element = document[key];

      if (element && element.type() == bsoncxx::type::k_string)
      {
        target = std::string(element.get_string().value);
        return true;
      }
      else
      {
        return false;
      }
    }


    std::string GetString(const bsoncxx::document::view& document,
                          const std::string& key)
    {
      std::string value;
      if (LookupString(value, document, key))
      {
        return value;
      }
      else
      {
        throw BadField(key);
      }
    }


    bsoncxx::types::b_binary ToBinary(const std::string& value)
    {
      bsoncxx::types::b_binary binary;
      binary.sub_type = bsoncxx::binary_sub_type::k_binary;
      binary.size = static_cast<uint32_t>(value.size());
      binary.bytes = reinterpret_cast<const uint8_t*>(value.data());
      return binary;
    }


    std::string GetBinary(const bsoncxx::document::view& document,
                          const std::string& key)
    {
      bsoncxx::document::element element = document[key];

      if (element &&
          element.type() == bsoncxx::type::k_binary)
      {
        const bsoncxx::types::b_binary binary = element.get_binary();
        return std::string(reinterpret_cast<const char*>(binary.bytes), binary.size);
      }
      else if (element &&
               element.type() == bsoncxx::type::k_string)
      {
        return std::string(element.get_string().value);
      }
      else
      {
        throw BadField(key);
      }
    }


    int64_t GetRevision(const bsoncxx::document::view& document)
    {
      int64_t revision;
      if (LookupInteger(revision, document, "revision"))
      {
        return revision;
      }
      else
      {
        return 0;
      }
    }


    bsoncxx::array::value ToArray(const std::list<int64_t>& values)
    {
      bsoncxx::builder::basic::array result;

      for (std::list<int64_t>::const_iterator it = values.begin(); it != values.end(); ++it)
      {
        result.append(*it);
      }

      return result.extract();
    }
  }
}
