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
#include "../../../Framework/Plugins/IDatabaseBackendOutput.h"

#include <list>


namespace OrthancDatabases
{
  /**
   * The "MainDicomTags" and "DicomIdentifiers" collections, both made
   * of documents { id, tagGroup, tagElement, value }. The identifiers
   * are the tags that Orthanc uses to look up resources.
   *
   * The study and series dates and times are also copied into the
   * "sorts" array of their resource, which sorts the lookups.
   **/
  class MongoDBMainDicomTags : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

    void SetTags(const char* collection,
                 uint32_t count,
                 const OrthancPluginResourcesContentTags* tags);

    void LookupIdentifiers(std::list<int64_t>& target,
                           const bsoncxx::document::view_or_value& filter);

  public:
    static const char* const MAIN_DICOM_TAGS;
    static const char* const DICOM_IDENTIFIERS;

    explicit MongoDBMainDicomTags(DatabaseManager& manager);

    void SetMainDicomTag(int64_t id,
                         uint16_t group,
                         uint16_t element,
                         const char* value);

    void SetIdentifierTag(int64_t id,
                          uint16_t group,
                          uint16_t element,
                          const char* value);

    // Several tags at once, cf. "SetResourcesContent()"
    void SetMainDicomTags(uint32_t count,
                          const OrthancPluginResourcesContentTags* tags)
    {
      SetTags(MAIN_DICOM_TAGS, count, tags);
    }

    void SetIdentifierTags(uint32_t count,
                           const OrthancPluginResourcesContentTags* tags)
    {
      SetTags(DICOM_IDENTIFIERS, count, tags);
    }

    void GetMainDicomTags(IDatabaseBackendOutput& output,
                          int64_t id);

    void ClearMainDicomTags(int64_t id);

    void LookupIdentifier(std::list<int64_t>& target /*out*/,
                          uint16_t group,
                          uint16_t element,
                          OrthancPluginIdentifierConstraint constraint,
                          const char* value);

    void LookupIdentifierRange(std::list<int64_t>& target /*out*/,
                               uint16_t group,
                               uint16_t element,
                               const char* start,
                               const char* end);

    void DeleteForResources(const std::list<int64_t>& resources);

    /**
     * Anchored regular expression for a DICOM wildcard ("*" and "?").
     * If "wildcards" is false, "*" and "?" are ordinary characters.
     **/
    static std::string ConvertWildcardToRegex(const std::string& wildcard,
                                              bool wildcards = true);
  };
}
