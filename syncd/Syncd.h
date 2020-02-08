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

            sai_status_t processQuadEventInInitViewMode(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& strObjectId,
                    _In_ sai_common_api_t api,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

            void processFlexCounterGroupEvent(
                    _In_ swss::ConsumerTable &consumer);

            void processFlexCounterEvent(
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

            sai_status_t processQuadEvent(
                    _In_ sai_common_api_t api,
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

        private: // process quad in init view mode

            sai_status_t processQuadInInitViewModeCreate(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& strObjectId,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

            sai_status_t processQuadInInitViewModeRemove(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& strObjectId);

            sai_status_t processQuadInInitViewModeSet(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& strObjectId,
                    _In_ sai_attribute_t *attr);

            sai_status_t processQuadInInitViewModeGet(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& strObjectId,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

        public: // TODO to private

            sai_status_t processEntry(
                    _In_ sai_object_meta_key_t &meta_key,
                    _In_ sai_common_api_t api,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

        public: // TODO private

            /**
             * @brief Send api response.
             *
             * This function should be use to send response to sairedis for
             * create/remove/set API as well as their corresponding bulk versions.
             *
             * Should not be used on GET api.
             */
            void sendApiResponse(
                    _In_ sai_common_api_t api,
                    _In_ sai_status_t status,
                    _In_ uint32_t object_count = 0,
                    _In_ sai_status_t * object_statuses = NULL);
        private:

            std::shared_ptr<CommandLineOptions> m_commandLineOptions;

            bool m_isWarmStart;

        public: // TODO to private

            bool m_asicInitViewMode;

            std::shared_ptr<FlexCounterManager> m_manager;

            std::shared_ptr<swss::ProducerTable> m_getResponse;

            /**
             * @brief set of objects removed by user when we are in init view
             * mode. Those could be vlan members, bridge ports etc.
             *
             * We need this list to later on not put them back to temp view
             * mode when doing populate existing objects in apply view mode.
             *
             * Object ids here a VIDs.
             */
            std::set<sai_object_id_t> m_initViewRemovedVidSet;
    };
}
