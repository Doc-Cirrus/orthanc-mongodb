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
   * The "AttachedFiles" collection { id, fileType, uuid,
   * compressedSize, uncompressedSize, compressionType,
   * uncompressedHash, compressedHash, revision }.
   *
   * The deleted attachments are signalled to Orthanc before they are
   * deleted. Without transactions, an interruption between both steps
   * leaves entries whose files are gone, which a retry deletes again;
   * the reverse order would leave files that nothing references.
   **/
  class MongoDBAttachments : public boost::noncopyable
  {
  private:
    MongoDBDatabase&  database_;

    void SignalAndDelete(IDatabaseBackendOutput& output,
                         const bsoncxx::document::view_or_value& filter);

  public:
    explicit MongoDBAttachments(DatabaseManager& manager);

    /**
     * The optional "customData" field is only written if "customData"
     * is not empty. Without transactions, throws if the resource was
     * deleted meanwhile: "DatabaseCannotSerialize" if the attachment
     * could be removed, so that Orthanc retries the store, and
     * "UnknownResource" if "DeleteResource()" has removed it and
     * signalled its file.
     **/
    void AddAttachment(int64_t id,
                       const OrthancPluginAttachment& attachment,
                       int64_t revision,
                       const std::string& customData);

    // Throws "UnknownResource" if no attachment has this UUID
    void GetCustomData(std::string& customData,
                       const std::string& uuid);

    void SetCustomData(const std::string& uuid,
                       const std::string& customData);

    void DeleteAttachment(IDatabaseBackendOutput& output,
                          int64_t id,
                          int32_t attachment);

    bool LookupAttachment(IDatabaseBackendOutput& output,
                          int64_t& revision /*out*/,
                          int64_t id,
                          int32_t contentType);

    void ListAvailableAttachments(std::list<int32_t>& target /*out*/,
                                  int64_t id);

    void DeleteForResources(IDatabaseBackendOutput& output,
                            const std::list<int64_t>& resources);

    /**
     * Without transactions, the second pass of "DeleteResource()", once
     * the resources are gone. Unlike "DeleteForResources()", it deletes
     * each attachment before signalling it, and only signals those it
     * deleted, as "AddAttachment()" may take them back meanwhile.
     **/
    void SweepForResources(IDatabaseBackendOutput& output,
                           const std::list<int64_t>& resources);
  };
}
