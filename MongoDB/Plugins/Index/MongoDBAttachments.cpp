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


#include "MongoDBAttachments.h"

#include "MongoDBStatistics.h"

#include "../../../Framework/MongoDB/MongoDBToolbox.h"

#include <OrthancException.h>


namespace OrthancDatabases
{
  using bsoncxx::builder::basic::kvp;
  using bsoncxx::builder::basic::make_document;


  static const char* const COLLECTION = "AttachedFiles";


  MongoDBAttachments::MongoDBAttachments(DatabaseManager& manager) :
    database_(MongoDBDatabase::GetDatabase(manager))
  {
  }


  // The documents written without custom data, e.g. by the previous versions, have no "customData"
  static std::string GetCustomDataField(const bsoncxx::document::view& attachment)
  {
    return (attachment["customData"] ? MongoDBToolbox::GetBinary(attachment, "customData") : std::string());
  }


  void MongoDBAttachments::SignalAndDelete(IDatabaseBackendOutput& output,
                                           const bsoncxx::document::view_or_value& filter)
  {
    MongoDBCollection collection = database_.GetCollection(COLLECTION);

    MongoDBCollection::Documents attachments;
    collection.Find(attachments, filter.view());

    for (size_t i = 0; i < attachments.size(); i++)
    {
      const bsoncxx::document::view attachment = attachments[i].view();
      output.SignalDeletedAttachment(MongoDBToolbox::GetString(attachment, "uuid"),
                                     MongoDBToolbox::GetInt32(attachment, "fileType"),
                                     MongoDBToolbox::GetInteger(attachment, "uncompressedSize"),
                                     MongoDBToolbox::GetString(attachment, "uncompressedHash"),
                                     MongoDBToolbox::GetInt32(attachment, "compressionType"),
                                     MongoDBToolbox::GetInteger(attachment, "compressedSize"),
                                     MongoDBToolbox::GetString(attachment, "compressedHash"),
                                     GetCustomDataField(attachment));
    }

    if (!attachments.empty())
    {
      // Not by "filter": without transactions, an attachment added since the
      // "Find()" would be deleted without being signalled, and its file kept
      bsoncxx::builder::basic::array ids;
      for (size_t i = 0; i < attachments.size(); i++)
      {
        ids.append(attachments[i].view()["_id"].get_value());
      }

      collection.DeleteMany(make_document(kvp("_id", make_document(kvp("$in", ids.extract())))));

      MongoDBStatisticsValues change;
      for (size_t i = 0; i < attachments.size(); i++)
      {
        change.compressedSize_ -= MongoDBToolbox::GetInteger(attachments[i].view(), "compressedSize");
        change.uncompressedSize_ -= MongoDBToolbox::GetInteger(attachments[i].view(), "uncompressedSize");
      }

      MongoDBStatistics(database_).RecordChange(change);
    }
  }


  void MongoDBAttachments::AddAttachment(int64_t id,
                                         const OrthancPluginAttachment& attachment,
                                         int64_t revision,
                                         const std::string& customData)
  {
    bsoncxx::builder::basic::document document;
    document.append(kvp("id", id),
                    kvp("fileType", attachment.contentType),
                    kvp("uuid", attachment.uuid),
                    kvp("compressedSize", static_cast<int64_t>(attachment.compressedSize)),
                    kvp("uncompressedSize", static_cast<int64_t>(attachment.uncompressedSize)),
                    kvp("compressionType", attachment.compressionType),
                    kvp("uncompressedHash", attachment.uncompressedHash),
                    kvp("compressedHash", attachment.compressedHash),
                    kvp("revision", revision));

    if (!customData.empty())
    {
      document.append(kvp("customData", MongoDBToolbox::ToBinary(customData)));
    }

    MongoDBCollection collection = database_.GetCollection(COLLECTION);
    collection.InsertOne(document.extract());

    MongoDBStatisticsValues change;
    change.compressedSize_ = static_cast<int64_t>(attachment.compressedSize);
    change.uncompressedSize_ = static_cast<int64_t>(attachment.uncompressedSize);
    MongoDBStatistics(database_).RecordChange(change);

    /**
     * Without transactions, "DeleteResource()" can delete the resource
     * while Orthanc stores it (e.g. a patient deleted while one of its
     * instances is overwritten). The attachment would then reference
     * nothing, and its file would never be deleted. "DeleteResource()"
     * sweeps the attachments again once the resources are gone, so
     * checking after the insertion catches what that sweep misses.
     *
     * Exactly one of both takes the attachment, as each only deletes
     * it with "findOneAndDelete". If the sweep took it, it has signalled
     * the file for deletion, so Orthanc must not retry the store with
     * that file: any error other than "DatabaseCannotSerialize" makes
     * Orthanc delete its files and fail the store.
     **/
    if (!database_.HasTransactions() &&
        !database_.GetCollection("Resources").Exists(make_document(kvp("internalId", id))))
    {
      if (collection.FindOneAndDelete(make_document(kvp("id", id), kvp("uuid", attachment.uuid))))
      {
        MongoDBStatisticsValues removal;
        removal -= change;
        MongoDBStatistics(database_).RecordChange(removal);

        // The file is still there, and the retry re-creates the resource
        throw Orthanc::OrthancException(Orthanc::ErrorCode_DatabaseCannotSerialize,
                                        "The resource of the attachment was deleted meanwhile");
      }
      else
      {
        throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource,
                                        "The resource of the attachment was deleted meanwhile, with the attachment");
      }
    }
  }


  void MongoDBAttachments::DeleteAttachment(IDatabaseBackendOutput& output,
                                            int64_t id,
                                            int32_t attachment)
  {
    SignalAndDelete(output, make_document(kvp("id", id), kvp("fileType", attachment)));
  }


  bool MongoDBAttachments::LookupAttachment(IDatabaseBackendOutput& output,
                                            int64_t& revision /*out*/,
                                            int64_t id,
                                            int32_t contentType)
  {
    std::optional<bsoncxx::document::value> document =
      database_.GetCollection(COLLECTION).FindOne(make_document(kvp("id", id), kvp("fileType", contentType)));

    if (document)
    {
      const bsoncxx::document::view attachment = document->view();
      output.AnswerAttachment(MongoDBToolbox::GetString(attachment, "uuid"),
                              contentType,
                              MongoDBToolbox::GetInteger(attachment, "uncompressedSize"),
                              MongoDBToolbox::GetString(attachment, "uncompressedHash"),
                              MongoDBToolbox::GetInt32(attachment, "compressionType"),
                              MongoDBToolbox::GetInteger(attachment, "compressedSize"),
                              MongoDBToolbox::GetString(attachment, "compressedHash"),
                              GetCustomDataField(attachment));

      revision = MongoDBToolbox::GetRevision(attachment);
      return true;
    }
    else
    {
      return false;
    }
  }


  void MongoDBAttachments::ListAvailableAttachments(std::list<int32_t>& target /*out*/,
                                                    int64_t id)
  {
    target.clear();

    MongoDBCollection::Documents attachments;
    database_.GetCollection(COLLECTION).Find(attachments, make_document(kvp("id", id)));

    for (size_t i = 0; i < attachments.size(); i++)
    {
      target.push_back(MongoDBToolbox::GetInt32(attachments[i].view(), "fileType"));
    }
  }


  void MongoDBAttachments::DeleteForResources(IDatabaseBackendOutput& output,
                                              const std::list<int64_t>& resources)
  {
    SignalAndDelete(output, make_document(kvp("id", make_document(kvp("$in", MongoDBToolbox::ToArray(resources))))));
  }


  void MongoDBAttachments::SweepForResources(IDatabaseBackendOutput& output,
                                             const std::list<int64_t>& resources)
  {
    MongoDBCollection collection = database_.GetCollection(COLLECTION);

    MongoDBCollection::Documents attachments;
    collection.Find(attachments, make_document(kvp("id", make_document(kvp("$in", MongoDBToolbox::ToArray(resources))))));

    MongoDBStatisticsValues change;

    for (size_t i = 0; i < attachments.size(); i++)
    {
      // "AddAttachment()" may take it back meanwhile, cf. its comment
      std::optional<bsoncxx::document::value> taken =
        collection.FindOneAndDelete(make_document(kvp("_id", attachments[i].view()["_id"].get_value())));

      if (taken)
      {
        const bsoncxx::document::view attachment = taken->view();
        output.SignalDeletedAttachment(MongoDBToolbox::GetString(attachment, "uuid"),
                                       MongoDBToolbox::GetInt32(attachment, "fileType"),
                                       MongoDBToolbox::GetInteger(attachment, "uncompressedSize"),
                                       MongoDBToolbox::GetString(attachment, "uncompressedHash"),
                                       MongoDBToolbox::GetInt32(attachment, "compressionType"),
                                       MongoDBToolbox::GetInteger(attachment, "compressedSize"),
                                       MongoDBToolbox::GetString(attachment, "compressedHash"),
                                       GetCustomDataField(attachment));

        change.compressedSize_ -= MongoDBToolbox::GetInteger(attachment, "compressedSize");
        change.uncompressedSize_ -= MongoDBToolbox::GetInteger(attachment, "uncompressedSize");
      }
    }

    if (!attachments.empty())
    {
      MongoDBStatistics(database_).RecordChange(change);
    }
  }


  void MongoDBAttachments::GetCustomData(std::string& customData,
                                         const std::string& uuid)
  {
    mongocxx::options::find options;
    options.projection(make_document(kvp("_id", 0), kvp("customData", 1)));

    std::optional<bsoncxx::document::value> attachment =
      database_.GetCollection(COLLECTION).FindOne(make_document(kvp("uuid", uuid)), options);

    if (attachment)
    {
      customData = GetCustomDataField(attachment->view());
    }
    else
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_UnknownResource, "Nonexistent attachment: " + uuid);
    }
  }


  void MongoDBAttachments::SetCustomData(const std::string& uuid,
                                         const std::string& customData)
  {
    if (customData.empty())
    {
      database_.GetCollection(COLLECTION).UpdateMany(
        make_document(kvp("uuid", uuid)),
        make_document(kvp("$unset", make_document(kvp("customData", "")))));
    }
    else
    {
      database_.GetCollection(COLLECTION).UpdateMany(
        make_document(kvp("uuid", uuid)),
        make_document(kvp("$set", make_document(kvp("customData", MongoDBToolbox::ToBinary(customData))))));
    }
  }
}
