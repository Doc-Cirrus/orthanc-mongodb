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


#include "MongoDBMainDicomTags.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"

#include <OrthancException.h>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  const char* const MongoDBMainDicomTags::MAIN_DICOM_TAGS = "MainDicomTags";
  const char* const MongoDBMainDicomTags::DICOM_IDENTIFIERS = "DicomIdentifiers";


  static bsoncxx::document::value CreateTag(int64_t id,
                                            uint16_t group,
                                            uint16_t element,
                                            const char* value)
  {
    return make_document(kvp("id", id),
                         kvp("tagGroup", group),
                         kvp("tagElement", element),
                         kvp("value", value));
  }


  // StudyDate, StudyTime, SeriesDate and SeriesTime
  static bool IsSortingTag(uint16_t group,
                           uint16_t element)
  {
    return (group == 0x0008 &&
            (element == 0x0020 || element == 0x0021 || element == 0x0030 || element == 0x0031));
  }


  MongoDBMainDicomTags::MongoDBMainDicomTags(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  void MongoDBMainDicomTags::SetTags(const char* collectionName,
                                     uint32_t count,
                                     const OrthancPluginResourcesContentTags* tags)
  {
    MongoDBCollection collection = database_.GetCollection(collectionName);
    MongoDBCollection resources = database_.GetCollection("Resources");

    mongocxx::bulk_write bulk = collection.CreateBulkWrite();

    for (uint32_t i = 0; i < count; i++)
    {
      bulk.append(mongocxx::model::insert_one(CreateTag(tags[i].resource, tags[i].group, tags[i].element, tags[i].value)));

      if (collectionName == MAIN_DICOM_TAGS &&
          IsSortingTag(tags[i].group, tags[i].element))
      {
        resources.UpdateOne(make_document(kvp("internalId", tags[i].resource)),
                            make_document(kvp("$addToSet", make_document(kvp("sorts", tags[i].value)))));
      }
    }

    collection.Execute(bulk);
  }


  void MongoDBMainDicomTags::SetMainDicomTag(int64_t id,
                                             uint16_t group,
                                             uint16_t element,
                                             const char* value)
  {
    database_.GetCollection(MAIN_DICOM_TAGS).InsertOne(CreateTag(id, group, element, value));
  }


  void MongoDBMainDicomTags::SetIdentifierTag(int64_t id,
                                              uint16_t group,
                                              uint16_t element,
                                              const char* value)
  {
    database_.GetCollection(DICOM_IDENTIFIERS).InsertOne(CreateTag(id, group, element, value));
  }


  void MongoDBMainDicomTags::GetMainDicomTags(IDatabaseBackendOutput& output,
                                              int64_t id)
  {
    MongoDBCollection::Documents tags;
    database_.GetCollection(MAIN_DICOM_TAGS).Find(tags, make_document(kvp("id", id)));

    for (size_t i = 0; i < tags.size(); i++)
    {
      const bsoncxx::document::view tag = tags[i].view();
      output.AnswerDicomTag(static_cast<uint16_t>(MongoDBToolbox::GetInt32(tag, "tagGroup")),
                            static_cast<uint16_t>(MongoDBToolbox::GetInt32(tag, "tagElement")),
                            MongoDBToolbox::GetString(tag, "value"));
    }
  }


  void MongoDBMainDicomTags::ClearMainDicomTags(int64_t id)
  {
    database_.GetCollection(MAIN_DICOM_TAGS).DeleteMany(make_document(kvp("id", id)));
    database_.GetCollection(DICOM_IDENTIFIERS).DeleteMany(make_document(kvp("id", id)));
  }


  void MongoDBMainDicomTags::LookupIdentifiers(std::list<int64_t>& target,
                                               const bsoncxx::document::view_or_value& filter)
  {
    target.clear();

    MongoDBCollection::Documents identifiers;
    database_.GetCollection(DICOM_IDENTIFIERS).Find(identifiers, filter);

    for (size_t i = 0; i < identifiers.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetInteger(identifiers[i].view(), "id"));
    }
  }


  void MongoDBMainDicomTags::LookupIdentifier(std::list<int64_t>& target /*out*/,
                                              uint16_t group,
                                              uint16_t element,
                                              OrthancPluginIdentifierConstraint constraint,
                                              const char* value)
  {
    bsoncxx::document::value condition = make_document();

    switch (constraint)
    {
      case OrthancPluginIdentifierConstraint_Equal:
        condition = make_document(kvp("$eq", value));
        break;

      case OrthancPluginIdentifierConstraint_SmallerOrEqual:
        condition = make_document(kvp("$lte", value));
        break;

      case OrthancPluginIdentifierConstraint_GreaterOrEqual:
        condition = make_document(kvp("$gte", value));
        break;

      case OrthancPluginIdentifierConstraint_Wildcard:
        condition = make_document(kvp("$regex", ConvertWildcardToRegex(value)), kvp("$options", "i"));
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    LookupIdentifiers(target, make_document(kvp("tagGroup", group),
                                            kvp("tagElement", element),
                                            kvp("value", condition.view())));
  }


  void MongoDBMainDicomTags::LookupIdentifierRange(std::list<int64_t>& target /*out*/,
                                                   uint16_t group,
                                                   uint16_t element,
                                                   const char* start,
                                                   const char* end)
  {
    LookupIdentifiers(target, make_document(kvp("tagGroup", group),
                                            kvp("tagElement", element),
                                            kvp("value", make_document(kvp("$gte", start), kvp("$lte", end)))));
  }


  void MongoDBMainDicomTags::DeleteForResources(const std::list<int64_t>& resources)
  {
    bsoncxx::document::value filter =
      make_document(kvp("id", make_document(kvp("$in", MongoDBToolbox::ToArray(resources)))));

    database_.GetCollection(MAIN_DICOM_TAGS).DeleteMany(filter.view());
    database_.GetCollection(DICOM_IDENTIFIERS).DeleteMany(filter.view());
  }


  std::string MongoDBMainDicomTags::ConvertWildcardToRegex(const std::string& wildcard,
                                                         bool wildcards)
  {
    std::string regex = "^";

    for (size_t i = 0; i < wildcard.size(); i++)
    {
      const char c = wildcard[i];

      switch (c)
      {
        case '*':
        case '?':
          if (wildcards)
          {
            regex += (c == '*' ? ".*" : ".");
          }
          else
          {
            regex += '\\';
            regex += c;
          }
          break;

        case '\\': case '^': case '$': case '.': case '|': case '+':
        case '(': case ')': case '[': case ']': case '{': case '}':
          regex += '\\';
          regex += c;
          break;

        default:
          regex += c;
          break;
      }
    }

    regex += '$';
    return regex;
  }
}
