#ifndef FLEX_COUNTER_H
#define FLEX_COUNTER_H

extern "C" {
#include "sai.h"
}

#include <atomic>
#include <vector>
#include <set>
#include <condition_variable>
#include <unordered_map>
#include "swss/table.h"
#include "swss/logger.h"

class FlexCounter
{
    public:
        static void setPollInterval(
                _In_ uint32_t pollInterval,
                _In_ std::string instanceId);
        static void setPortCounterList(
                _In_ sai_object_id_t portVid,
                _In_ sai_object_id_t portId,
                _In_ std::string instanceId,
                _In_ const std::vector<sai_port_stat_t> &counterIds);
        static void setPortDebugCounterList(
                _In_ sai_object_id_t portVid,
                _In_ sai_object_id_t portId,
                _In_ std::string instanceId,
                _In_ const std::vector<sai_port_stat_t> &counterIds);
        static void setQueueCounterList(
                _In_ sai_object_id_t queueVid,
                _In_ sai_object_id_t queueId,
                _In_ std::string instanceId,
                _In_ const std::vector<sai_queue_stat_t> &counterIds);
        static void setPriorityGroupCounterList(
                _In_ sai_object_id_t priorityGroupVid,
                _In_ sai_object_id_t priorityGroupId,
                _In_ std::string instanceId,
                _In_ const std::vector<sai_ingress_priority_group_stat_t> &counterIds);
        static void setRifCounterList(
                _In_ sai_object_id_t rifVid,
                _In_ sai_object_id_t rifId,
                _In_ std::string instanceId,
                _In_ const std::vector<sai_router_interface_stat_t> &counterIds);
        static void setBufferPoolCounterList(
                _In_ sai_object_id_t bufferPoolVid,
                _In_ sai_object_id_t bufferPoolId,
                _In_ const std::string &instanceId,
                _In_ const std::vector<sai_buffer_pool_stat_t> &counterIds,
                _In_ const std::string &statsMode = "");
        static void setSwitchDebugCounterList(
                _In_ sai_object_id_t switchVid,
                _In_ sai_object_id_t switchId,
                _In_ std::string instanceId,
                _In_ const std::vector<sai_switch_stat_t> &counterIds);
        static void setQueueAttrList(
                _In_ sai_object_id_t queueVid,
                _In_ sai_object_id_t queueId,
                _In_ std::string instanceId,
                _In_ const std::vector<sai_queue_attr_t> &attrIds);
        static void setPriorityGroupAttrList(
                _In_ sai_object_id_t priorityGroupVid,
                _In_ sai_object_id_t priorityGroupId,
                _In_ std::string instanceId,
                _In_ const std::vector<sai_ingress_priority_group_attr_t> &attrIds);
        static void updateFlexCounterStatus(
                _In_ std::string status,
                _In_ std::string instanceId);
        static void updateFlexCounterStatsMode(
                 _In_ std::string mode,
                 _In_ std::string instanceId);

        static void removePort(
                _In_ sai_object_id_t portVid,
                _In_ std::string instanceId);
        static void removePortDebugCounters(
                _In_ sai_object_id_t portVid,
                _In_ std::string instanceId);
        static void removeQueue(
                _In_ sai_object_id_t queueVid,
                _In_ std::string instanceId);
        static void removePriorityGroup(
                _In_ sai_object_id_t priorityGroupVid,
                _In_ std::string instanceId);
        static void removeRif(
                _In_ sai_object_id_t rifVid,
                _In_ std::string instanceId);
        static void removeBufferPool(
                _In_ sai_object_id_t bufferPoolVid,
                _In_ std::string instanceId);
        static void removeSwitchDebugCounters(
                _In_ sai_object_id_t switchVid,
                _In_ std::string instanceId);

        static void addPortCounterPlugin(
                _In_ std::string sha,
                _In_ std::string instanceId);
        static void addQueueCounterPlugin(
                _In_ std::string sha,
                _In_ std::string instanceId);
        static void addPriorityGroupCounterPlugin(
                _In_ std::string sha,
                _In_ std::string instanceId);
        static void addBufferPoolCounterPlugin(
                _In_ std::string sha,
                _In_ std::string instanceId);

        static void removeCounterPlugin(
                _In_ std::string instanceId);

        static void addCounterPlugin(
                _In_ const std::string& groupName,
                _In_ const std::vector<swss::FieldValueTuple>& values);

        static void removeCounter(
                _In_ sai_object_id_t vid,
                _In_ const std::string& groupName);

        static void removeAllCounters();

        FlexCounter(
                _In_ const FlexCounter&) = delete;
        ~FlexCounter(void);

    private:
        struct QueueCounterIds
        {
            QueueCounterIds(
                    _In_ sai_object_id_t queue,
                    _In_ const std::vector<sai_queue_stat_t> &queueIds);

            sai_object_id_t queueId;
            std::vector<sai_queue_stat_t> queueCounterIds;
        };

        struct QueueAttrIds
        {
            QueueAttrIds(
                    _In_ sai_object_id_t queue,
                    _In_ const std::vector<sai_queue_attr_t> &queueIds);

            sai_object_id_t queueId;
            std::vector<sai_queue_attr_t> queueAttrIds;
        };

        struct IngressPriorityGroupCounterIds
        {
            IngressPriorityGroupCounterIds(
                    _In_ sai_object_id_t priorityGroup,
                    _In_ const std::vector<sai_ingress_priority_group_stat_t> &priorityGroupIds);

            sai_object_id_t priorityGroupId;
            std::vector<sai_ingress_priority_group_stat_t> priorityGroupCounterIds;
        };

        struct IngressPriorityGroupAttrIds
        {
            IngressPriorityGroupAttrIds(
                    _In_ sai_object_id_t priorityGroup,
                    _In_ const std::vector<sai_ingress_priority_group_attr_t> &priorityGroupIds);

            sai_object_id_t priorityGroupId;
            std::vector<sai_ingress_priority_group_attr_t> priorityGroupAttrIds;
        };

        struct BufferPoolCounterIds
        {
            BufferPoolCounterIds(
                _In_ sai_object_id_t bufferPool,
                _In_ const std::vector<sai_buffer_pool_stat_t> &bufferPoolIds,
                _In_ sai_stats_mode_t statsMode);

            sai_object_id_t bufferPoolId;
            sai_stats_mode_t bufferPoolStatsMode;
            std::vector<sai_buffer_pool_stat_t> bufferPoolCounterIds;
        };

        struct PortCounterIds
        {
            PortCounterIds(
                    _In_ sai_object_id_t port,
                    _In_ const std::vector<sai_port_stat_t> &portIds);

            sai_object_id_t portId;
            std::vector<sai_port_stat_t> portCounterIds;
        };

        struct SwitchCounterIds
        {
            SwitchCounterIds(
                _In_ sai_object_id_t oid,
                _In_ const std::vector<sai_switch_stat_t> &counterIds);

            sai_object_id_t switchId;
            std::vector<sai_switch_stat_t> switchCounterIds;
        };

        struct RifCounterIds
        {
            RifCounterIds(
                    _In_ sai_object_id_t rif,
                    _In_ const std::vector<sai_router_interface_stat_t> &rifIds);

            sai_object_id_t rifId;
            std::vector<sai_router_interface_stat_t> rifCounterIds;
        };

        FlexCounter(std::string instanceId);
        static FlexCounter& getInstance(std::string instanceId);
        static void removeInstance(std::string instanceId);

        void collectCounters(_In_ swss::Table &countersTable);
        void runPlugins(_In_ swss::DBConnector& db);
        void flexCounterThread(void);
        void startFlexCounterThread(void);
        void endFlexCounterThread(void);

        void saiUpdateSupportedPortCounters(sai_object_id_t portId);
        void saiUpdateSupportedQueueCounters(sai_object_id_t queueId, const std::vector<sai_queue_stat_t> &counterIds);
        void saiUpdateSupportedPriorityGroupCounters(sai_object_id_t priorityGroupId, const std::vector<sai_ingress_priority_group_stat_t> &counterIds);
        void saiUpdateSupportedRifCounters(sai_object_id_t rifId);
        void saiUpdateSupportedBufferPoolCounters(sai_object_id_t bufferPoolId,
                                                  const std::vector<sai_buffer_pool_stat_t> &counterIds,
                                                  sai_stats_mode_t statsMode = SAI_STATS_MODE_READ_AND_CLEAR);

        std::vector<sai_port_stat_t> saiCheckSupportedPortDebugCounters(
                _In_ sai_object_id_t portId,
                _In_ const std::vector<sai_port_stat_t> &counterIds);
        std::vector<sai_switch_stat_t> saiCheckSupportedSwitchDebugCounters(
                _In_ sai_object_id_t switchId,
                _In_ const std::vector<sai_switch_stat_t> &counterIds);

        bool isPortCounterSupported(sai_port_stat_t counter) const;
        bool isQueueCounterSupported(sai_queue_stat_t counter) const;
        bool isPriorityGroupCounterSupported(sai_ingress_priority_group_stat_t counter) const;
        bool isRifCounterSupported(sai_router_interface_stat_t counter) const;
        bool isBufferPoolCounterSupported(sai_buffer_pool_stat_t counter) const;
        bool isEmpty();
        bool allIdsEmpty();
        bool allPluginsEmpty();

        typedef void (FlexCounter::*collect_counters_handler_t)(_In_ swss::Table &countersTable);
        typedef std::unordered_map<std::string, collect_counters_handler_t> collect_counters_handler_unordered_map_t;

        void collectPortCounters(_In_ swss::Table &countersTable);
        void collectPortDebugCounters(_In_ swss::Table &countersTable);
        void collectQueueCounters(_In_ swss::Table &countersTable);
        void collectQueueAttrs(_In_ swss::Table &countersTable);
        void collectPriorityGroupCounters(_In_ swss::Table &countersTable);
        void collectPriorityGroupAttrs(_In_ swss::Table &countersTable);
        void collectRifCounters(_In_ swss::Table &countersTable);
        void collectBufferPoolCounters(_In_ swss::Table &countersTable);
        void collectSwitchDebugCounters(_In_ swss::Table &countersTable);

        void addCollectCountersHandler(const std::string &key, const collect_counters_handler_t &handler);
        void removeCollectCountersHandler(const std::string &key);

        // Key is a Virtual ID
        std::map<sai_object_id_t, std::shared_ptr<PortCounterIds>> m_portCounterIdsMap;
        std::map<sai_object_id_t, std::shared_ptr<PortCounterIds>> m_portDebugCounterIdsMap;
        std::map<sai_object_id_t, std::shared_ptr<QueueCounterIds>> m_queueCounterIdsMap;
        std::map<sai_object_id_t, std::shared_ptr<QueueAttrIds>> m_queueAttrIdsMap;
        std::map<sai_object_id_t, std::shared_ptr<IngressPriorityGroupCounterIds>> m_priorityGroupCounterIdsMap;
        std::map<sai_object_id_t, std::shared_ptr<IngressPriorityGroupAttrIds>> m_priorityGroupAttrIdsMap;
        std::map<sai_object_id_t, std::shared_ptr<RifCounterIds>> m_rifCounterIdsMap;
        std::map<sai_object_id_t, std::shared_ptr<BufferPoolCounterIds>> m_bufferPoolCounterIdsMap;
        std::map<sai_object_id_t, std::shared_ptr<SwitchCounterIds>> m_switchDebugCounterIdsMap;

        // Plugins
        std::set<std::string> m_queuePlugins;
        std::set<std::string> m_portPlugins;
        std::set<std::string> m_priorityGroupPlugins;
        std::set<std::string> m_bufferPoolPlugins;

        bool m_runFlexCounterThread = false;
        std::shared_ptr<std::thread> m_flexCounterThread = nullptr;
        std::mutex m_mtxSleep;
        std::condition_variable m_cvSleep;

        std::mutex m_mtx;
        std::condition_variable m_pollCond;
        uint32_t m_pollInterval = 0;
        std::string m_instanceId;
        sai_stats_mode_t m_statsMode;
        bool m_enable = false;

        collect_counters_handler_unordered_map_t m_collectCountersHandlers;
};

#endif
