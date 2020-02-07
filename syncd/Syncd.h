#pragma once

#include "CommandLineOptions.h"
#include "FlexCounterManager.h"

#include "meta/saiattributelist.h"

#include "swss/consumertable.h"
#include "swss/producertable.h"

#include <memory>

namespace syncd
{
    class Syncd
    {
        private:

            Syncd(const Syncd&) = delete;
            Syncd& operator=(const Syncd&) = delete;

            public:

            Syncd(
                    _In_ std::shared_ptr<CommandLineOptions> cmd,
                    _In_ bool isWarmStart);

            virtual ~Syncd();

        public:

            bool getAsicInitViewMode() const;

            void setAsicInitViewMode(
                    _In_ bool enable);

            bool isInitViewMode() const;

        public: // TODO private

            void processEvent(
                    _In_ swss::ConsumerTable &consumer);

        private:

            sai_status_t processSingleEvent(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processAttrEnumValuesCapabilityQuery(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processObjectTypeGetAvailabilityQuery(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processFdbFlush(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processClearStatsEvent(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processGetStatsEvent(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processBulkQuadEvent(
                    _In_ sai_common_api_t api,
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processBulkOid(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::vector<std::string> &object_ids,
                    _In_ sai_common_api_t api,
                    _In_ const std::vector<std::shared_ptr<SaiAttributeList>> &attributes);

            sai_status_t processBulkEntry(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::vector<std::string> &object_ids,
                    _In_ sai_common_api_t api,
                    _In_ const std::vector<std::shared_ptr<SaiAttributeList>> &attributes);

        public: // TODO to private

            sai_status_t processEntry(
                    _In_ sai_object_meta_key_t &meta_key,
                    _In_ sai_common_api_t api,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

        private:

            std::shared_ptr<CommandLineOptions> m_commandLineOptions;

            bool m_isWarmStart;

        public: // TODO to private

            bool m_asicInitViewMode;

            std::shared_ptr<FlexCounterManager> m_manager;

            std::shared_ptr<swss::ProducerTable> m_getResponse;

    };
}
