// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#include "../../Resources/Orthanc/Databases/DatabaseConstraint.h"

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
                                         const std::string& compressedHash) = 0;

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
                                  const std::string& compressedHash) = 0;

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
