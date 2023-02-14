

#pragma once

#include <mongocxx/cursor.hpp>
#include <bsoncxx/document/value.hpp>

#include "../../Framework/Plugins/IndexBackend.h"

namespace OrthancDatabases {
    class MongoDBIndex : public IndexBackend {
    private:
        std::string url_;
        int chunkSize_;

    protected:
        // methods overriden for mongodb
        void SignalDeletedFiles(
                IDatabaseBackendOutput &output,
                mongocxx::cursor& cursor
        );

        void SignalDeletedResources(
                IDatabaseBackendOutput &output,
                const std::vector <bsoncxx::document::view> deleted_resources_vec
        );

    public:
        explicit MongoDBIndex(OrthancPluginContext *context);  // Opens in memory

        MongoDBIndex(OrthancPluginContext *context, const std::string &url_, const int &chunkSize_);

        IDatabaseFactory *CreateDatabaseFactory() override;

        void ConfigureDatabase(DatabaseManager &manager) override;

        bool HasRevisionsSupport() const override {
            return true;
        }

        //
        virtual void AddAttachment(DatabaseManager &manager, int64_t id, const OrthancPluginAttachment &attachment,
                                   int64_t revision) override;

        virtual void AttachChild(DatabaseManager &manager, int64_t parent, int64_t child) override;

        virtual void ClearChanges(DatabaseManager &manager) override;

        virtual void ClearExportedResources(DatabaseManager &manager) override;

        virtual void DeleteAttachment(IDatabaseBackendOutput &output,
                                      DatabaseManager &manager,
                                      int64_t id,
                                      int32_t attachment) override;

        virtual void DeleteMetadata(DatabaseManager &manager,
                                    int64_t id,
                                    int32_t metadataType) override;

        virtual void DeleteResource(IDatabaseBackendOutput &output,
                                    DatabaseManager &manager,
                                    int64_t id) override;

        virtual void GetAllInternalIds(std::list <int64_t> &target,
                                       DatabaseManager &manager,
                                       OrthancPluginResourceType resourceType) override;

        virtual void GetAllPublicIds(std::list <std::string> &target,
                                     DatabaseManager &manager,
                                     OrthancPluginResourceType resourceType) override;

        virtual void GetAllPublicIds(std::list <std::string> &target,
                                     DatabaseManager &manager,
                                     OrthancPluginResourceType resourceType,
                                     uint64_t since,
                                     uint64_t limit) override;

        virtual void GetChanges(IDatabaseBackendOutput &output,
                                bool &done /*out*/,
                                DatabaseManager &manager,
                                int64_t since,
                                uint32_t maxResults) override;

        virtual void GetChildrenInternalId(std::list <int64_t> &target /*out*/,
                                           DatabaseManager &manager,
                                           int64_t id) override;

        virtual void GetChildrenPublicId(std::list <std::string> &target /*out*/,
                                         DatabaseManager &manager,
                                         int64_t id) override;

        virtual void GetExportedResources(IDatabaseBackendOutput &output,
                                          bool &done /*out*/,
                                          DatabaseManager &manager,
                                          int64_t since,
                                          uint32_t maxResults) override;

        virtual void GetLastChange(IDatabaseBackendOutput &output,
                                   DatabaseManager &manager) override;

        virtual void GetLastExportedResource(IDatabaseBackendOutput &output,
                                             DatabaseManager &manager) override;

        virtual void GetMainDicomTags(IDatabaseBackendOutput &output,
                                      DatabaseManager &manager,
                                      int64_t id) override;

        virtual std::string GetPublicId(DatabaseManager &manager,
                                        int64_t resourceId) override;

        virtual uint64_t GetResourcesCount(DatabaseManager &manager,
                                           OrthancPluginResourceType resourceType) override;

        virtual OrthancPluginResourceType GetResourceType(DatabaseManager &manager,
                                                          int64_t resourceId) override;

        virtual uint64_t GetTotalCompressedSize(DatabaseManager &manager) override;

        virtual uint64_t GetTotalUncompressedSize(DatabaseManager &manager) override;

        virtual bool IsExistingResource(DatabaseManager &manager,
                                        int64_t internalId) override;

        virtual bool IsProtectedPatient(DatabaseManager &manager,
                                        int64_t internalId) override;

        virtual void ListAvailableMetadata(std::list <int32_t> &target /*out*/,
                                           DatabaseManager &manager,
                                           int64_t id) override;

        virtual void ListAvailableAttachments(std::list <int32_t> &target /*out*/,
                                              DatabaseManager &manager,
                                              int64_t id) override;

        virtual void LogChange(DatabaseManager &manager,
                               int32_t changeType,
                               int64_t resourceId,
                               OrthancPluginResourceType resourceType,
                               const char *date) override;

        virtual void LogExportedResource(DatabaseManager &manager,
                                         const OrthancPluginExportedResource &resource) override;

        virtual bool LookupAttachment(IDatabaseBackendOutput &output,
                                      int64_t &revision /*out*/,
                                      DatabaseManager &manager,
                                      int64_t id,
                                      int32_t contentType) override;

        bool LookupGlobalProperty(std::string &target /*out*/, DatabaseManager &manager, const char *serverIdentifier,
                                  int32_t property) override;

        virtual void LookupIdentifier(std::list <int64_t> &target /*out*/,
                                      DatabaseManager &manager,
                                      OrthancPluginResourceType resourceType,
                                      uint16_t group,
                                      uint16_t element,
                                      OrthancPluginIdentifierConstraint constraint,
                                      const char *value) override;

        virtual void LookupIdentifierRange(std::list <int64_t> &target /*out*/,
                                           DatabaseManager &manager,
                                           OrthancPluginResourceType resourceType,
                                           uint16_t group,
                                           uint16_t element,
                                           const char *start,
                                           const char *end) override;

        virtual bool LookupMetadata(std::string &target /*out*/,
                                    int64_t &revision /*out*/,
                                    DatabaseManager &manager,
                                    int64_t id,
                                    int32_t metadataType) override;

        virtual bool LookupParent(int64_t &parentId /*out*/,
                                  DatabaseManager &manager,
                                  int64_t resourceId) override;

        virtual bool LookupResource(int64_t &id /*out*/,
                                    OrthancPluginResourceType &type /*out*/,
                                    DatabaseManager &manager,
                                    const char *publicId) override;

        virtual bool SelectPatientToRecycle(int64_t &internalId /*out*/,
                                            DatabaseManager &manager) override;

        virtual bool SelectPatientToRecycle(int64_t &internalId /*out*/,
                                            DatabaseManager &manager,
                                            int64_t patientIdToAvoid) override;

        void SetGlobalProperty(DatabaseManager &manager, const char *serverIdentifier, int32_t property,
                               const char *utf8) override;

        virtual void SetMainDicomTag(DatabaseManager &manager,
                                     int64_t id,
                                     uint16_t group,
                                     uint16_t element,
                                     const char *value) override;

        virtual void SetIdentifierTag(DatabaseManager &manager,
                                      int64_t id,
                                      uint16_t group,
                                      uint16_t element,
                                      const char *value) override;

        virtual void SetMetadata(DatabaseManager &manager,
                                 int64_t id,
                                 int32_t metadataType,
                                 const char *value,
                                 int64_t revision) override;

        virtual void SetProtectedPatient(DatabaseManager &manager,
                                         int64_t internalId,
                                         bool isProtected) override;

        virtual void ClearMainDicomTags(DatabaseManager &manager,
                                        int64_t internalId) override;

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1

        // New primitive since Orthanc 1.5.2
        virtual void LookupResources(IDatabaseBackendOutput &output,
                                     DatabaseManager &manager,
                                     const std::vector <Orthanc::DatabaseConstraint> &lookup,
                                     OrthancPluginResourceType queryLevel,
                                     uint32_t limit,
                                     bool requestSomeInstance) override;

#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1

        // New primitive since Orthanc 1.5.2
        virtual void SetResourcesContent(
                DatabaseManager &manager,
                uint32_t countIdentifierTags,
                const OrthancPluginResourcesContentTags *identifierTags,
                uint32_t countMainDicomTags,
                const OrthancPluginResourcesContentTags *mainDicomTags,
                uint32_t countMetadata,
                const OrthancPluginResourcesContentMetadata *metadata) override;

#endif

        // New primitive since Orthanc 1.5.2
        virtual void GetChildrenMetadata(std::list <std::string> &target,
                                         DatabaseManager &manager,
                                         int64_t resourceId,
                                         int32_t metadata) override;

        virtual void TagMostRecentPatient(DatabaseManager &manager,
                                          int64_t patient) override;

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)

        // New primitive since Orthanc 1.5.4
        virtual bool LookupResourceAndParent(int64_t &id,
                                             OrthancPluginResourceType &type,
                                             std::string &parentPublicId,
                                             DatabaseManager &manager,
                                             const char *publicId) override;

#  endif
#endif

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)      // Macro introduced in 1.3.1
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 4)

        // New primitive since Orthanc 1.5.4
        virtual void GetAllMetadata(std::map <int32_t, std::string> &result,
                                    DatabaseManager &manager,
                                    int64_t id) override;

#  endif
#endif

        virtual bool HasCreateInstance() const override {
            // This extension is available in PostgreSQL and MySQL, but is
            // emulated by "CreateInstanceGeneric()" in SQLite
            return true;
        }

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
        virtual void CreateInstance(OrthancPluginCreateInstanceResult &result,
                                    DatabaseManager &manager,
                                    const char *hashPatient,
                                    const char *hashStudy,
                                    const char *hashSeries,
                                    const char *hashInstance) override;

#endif

        int64_t CreateResource(DatabaseManager &manager,
                               const char *publicId,
                               OrthancPluginResourceType type) override {
            return -1;
        }

        // New primitive since Orthanc 1.5.2
        int64_t GetLastChangeIndex(DatabaseManager &manager) override {
            return -1;
        }

        // methods overriden for mongodb
    };
}
