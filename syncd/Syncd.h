#pragma once

#include "CommandLineOptions.h"
#include "FlexCounterManager.h"
#include "VendorSai.h"
#include "AsicView.h"

#include "meta/SaiAttributeList.h"

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
                    _In_ std::shared_ptr<sairedis::SaiInterface> vendorSai,
                    _In_ std::shared_ptr<CommandLineOptions> cmd,
                    _In_ bool isWarmStart);

            virtual ~Syncd();

        public:

            bool getAsicInitViewMode() const;

            void setAsicInitViewMode(
                    _In_ bool enable);

            bool isInitViewMode() const;

            void onSyncdStart(
                    _In_ bool warmStart);

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

            const char* profileGetValue(
                    _In_ sai_switch_profile_id_t profile_id,
                    _In_ const char* variable);

            int profileGetNextValue(
                    _In_ sai_switch_profile_id_t profile_id,
                    _Out_ const char** variable,
                    _Out_ const char** value);

            void performStartupLogic();

            void sendShutdownRequest(
                    _In_ sai_object_id_t switchVid);

            void sendShutdownRequestAfterException();

        public: // shutdown actions for all switches

            sai_status_t removeAllSwitches();

            sai_status_t setRestartWarmOnAllSwitches(
                    _In_ bool flag);

            sai_status_t setPreShutdownOnAllSwitches();

            sai_status_t setUninitDataPlaneOnRemovalOnAllSwitches();

        private:

            void loadProfileMap();

            void saiLoglevelNotify(
                    _In_ std::string strApi,
                    _In_ std::string strLogLevel);

            void setSaiApiLogLevel();

        private:

            sai_status_t processNotifySyncd(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

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
                    _In_ sai_object_type_t objectType,
                    _In_ const std::vector<std::string> &object_ids,
                    _In_ sai_common_api_t api,
                    _In_ const std::vector<std::shared_ptr<saimeta::SaiAttributeList>> &attributes);

            sai_status_t processBulkEntry(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::vector<std::string> &object_ids,
                    _In_ sai_common_api_t api,
                    _In_ const std::vector<std::shared_ptr<saimeta::SaiAttributeList>> &attributes);

            sai_status_t processOid(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string &strObjectId,
                    _In_ sai_common_api_t api,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

        private: // process quad oid

            sai_status_t processOidCreate(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string &strObjectId,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

            sai_status_t processOidRemove(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string &strObjectId);

            sai_status_t processOidSet(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string &strObjectId,
                    _In_ sai_attribute_t *attr);

            sai_status_t processOidGet(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string &strObjectId,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

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

        private:

            void inspectAsic();

            void clearTempView();

            sai_status_t onApplyViewInFastFastBoot();

            sai_status_t applyView();

            void dumpComparisonLogicOutput(
                    _In_ const std::vector<std::shared_ptr<AsicView>>& currentViews);

            void updateRedisDatabase(
                    _In_ const std::vector<std::shared_ptr<AsicView>>& temporaryViews);

            std::map<sai_object_id_t, swss::TableDump> redisGetAsicView(
                    _In_ const std::string &tableName);

            void onSwitchCreateInInitViewMode(
                    _In_ sai_object_id_t switch_vid,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            void performWarmRestart();

            void performWarmRestartSingleSwitch(
                    _In_ const std::string& key);

            void startDiagShell(
                    _In_ sai_object_id_t switchRid);

            void diagShellThreadProc(
                    _In_ sai_object_id_t switchRid);

        private:

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

            void sendGetResponse(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& strObjectId,
                    _In_ sai_object_id_t switchVid,
                    _In_ sai_status_t status,
                    _In_ uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

            void sendNotifyResponse(
                    _In_ sai_status_t status);

        private: // snoop get response oids

            void snoopGetResponse(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::string &strObjectId,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            void snoopGetAttr(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& strObjectId,
                    _In_ const std::string& attrId,
                    _In_ const std::string& attrValue);

            void snoopGetOid(
                    _In_ sai_object_id_t vid);

            void snoopGetOidList(
                    _In_ const sai_object_list_t& list);

            void snoopGetAttrValue(
                    _In_ const std::string& strObjectId,
                    _In_ const sai_attr_metadata_t *meta,
                    _In_ const sai_attribute_t& attr);

        private:

            std::shared_ptr<CommandLineOptions> m_commandLineOptions;

            bool m_isWarmStart;

            bool m_firstInitWasPerformed;

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

            std::shared_ptr<sairedis::SaiInterface> m_vendorSai;

            /*
             * TODO: Those are hard coded values for mlnx integration for v1.0.1 they need
             * to be updated.
             *
             * Also DEVICE_MAC_ADDRESS is not present in saiswitch.h
             */
            std::map<std::string, std::string> m_profileMap;

            std::map<std::string, std::string>::iterator m_profileIter;

    };
}
