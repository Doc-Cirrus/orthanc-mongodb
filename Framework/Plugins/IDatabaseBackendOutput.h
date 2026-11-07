/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#pragma once

#include "DatabaseConstraint.h"

namespace OrthancDatabases
{
  class IDatabaseBackendOutput : public boost::noncopyable
  {
  public:
    /**
     * Contrarily to its parent "IDatabaseBackendOutput" class, the
     * "IFactory" subclass *can* be invoked from multiple threads if
     * used through "DatabaseBackendAdapterV3". Make sure to implement
     * proper locking if need be.
     **/
    class IFactory : public boost::noncopyable
    {
    public:
      virtual ~IFactory()
      {
      }

      virtual IDatabaseBackendOutput* CreateOutput() = 0;
    };
    
    virtual ~IDatabaseBackendOutput()
    {
    }

    virtual void SignalDeletedAttachment(const std::string& uuid,
                                         int32_t            contentType,
                                         uint64_t           uncompressedSize,
                                         const std::string& uncompressedHash,
                                         int32_t            compressionType,
                                         uint64_t           compressedSize,
                                         const std::string& compressedHash,
                                         const std::string& customData) = 0;

    virtual void SignalDeletedResource(const std::string& publicId,
                                       OrthancPluginResourceType resourceType) = 0;

    virtual void SignalRemainingAncestor(const std::string& ancestorId,
                                         OrthancPluginResourceType ancestorType) = 0;
    
    virtual void AnswerAttachment(const std::string& uuid,
                                  int32_t            contentType,
                                  uint64_t           uncompressedSize,
                                  const std::string& uncompressedHash,
                                  int32_t            compressionType,
                                  uint64_t           compressedSize,
                                  const std::string& compressedHash,
                                  const std::string& customData) = 0;

    virtual void AnswerChange(int64_t                    seq,
                              int32_t                    changeType,
                              OrthancPluginResourceType  resourceType,
                              const std::string&         publicId,
                              const std::string&         date) = 0;

    virtual void AnswerDicomTag(uint16_t group,
                                uint16_t element,
                                const std::string& value) = 0;

    virtual void AnswerExportedResource(int64_t                    seq,
                                        OrthancPluginResourceType  resourceType,
                                        const std::string&         publicId,
                                        const std::string&         modality,
                                        const std::string&         date,
                                        const std::string&         patientId,
                                        const std::string&         studyInstanceUid,
                                        const std::string&         seriesInstanceUid,
                                        const std::string&         sopInstanceUid) = 0;
    
#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void AnswerMatchingResource(const std::string& resourceId) = 0;
#endif
    
#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    virtual void AnswerMatchingResource(const std::string& resourceId,
                                        const std::string& someInstanceId) = 0;
#endif
  };
}
