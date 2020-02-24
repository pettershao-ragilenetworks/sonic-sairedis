#pragma once

#include "SwitchState.h"
#include "FdbInfo.h"
#include "HostInterfaceInfo.h"
#include "WarmBootState.h"
#include "SwitchConfig.h"
#include "RealObjectIdManager.h"
#include "EventPayloadNetLinkMsg.h"

#include <set>
#include <unordered_set>

#define SAI_VS_FDB_INFO "SAI_VS_FDB_INFO"

#define DEFAULT_VLAN_NUMBER 1

#define CHECK_STATUS(status) {                                  \
    sai_status_t _status = (status);                            \
    if (_status != SAI_STATUS_SUCCESS) { return _status; } }

namespace saivs
{
    class SwitchStateBase:
        public SwitchState
    {
        public:

            typedef std::map<sai_object_id_t, std::shared_ptr<SwitchStateBase>> SwitchStateMap;

        public:

            SwitchStateBase(
                    _In_ sai_object_id_t switch_id,
                    _In_ std::shared_ptr<RealObjectIdManager> manager,
                    _In_ std::shared_ptr<SwitchConfig> config);

            SwitchStateBase(
                    _In_ sai_object_id_t switch_id,
                    _In_ std::shared_ptr<RealObjectIdManager> manager,
                    _In_ std::shared_ptr<SwitchConfig> config,
                    std::shared_ptr<WarmBootState> warmBootState);

            virtual ~SwitchStateBase();

        protected:

            virtual sai_status_t set_switch_mac_address();

            virtual sai_status_t set_switch_default_attributes();

            virtual sai_status_t create_default_vlan();

            virtual sai_status_t create_cpu_port();

            virtual sai_status_t create_default_1q_bridge();

            virtual sai_status_t create_ports();

            virtual sai_status_t set_port_list();

            virtual sai_status_t create_default_virtual_router();

            virtual sai_status_t create_default_stp_instance();

            virtual sai_status_t create_default_trap_group();

            virtual sai_status_t create_ingress_priority_groups_per_port(
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t create_ingress_priority_groups();

            virtual sai_status_t create_vlan_members();

            virtual sai_status_t create_bridge_ports();

            virtual sai_status_t set_acl_entry_min_prio();

            virtual sai_status_t set_acl_capabilities();

            virtual sai_status_t set_maximum_number_of_childs_per_scheduler_group();

            virtual sai_status_t set_number_of_ecmp_groups();

        public:

            virtual sai_status_t initialize_default_objects();

            virtual sai_status_t create_port_dependencies(
                    _In_ sai_object_id_t port_id);

        protected : // refresh

            virtual sai_status_t refresh_ingress_priority_group(
                    _In_ const sai_attr_metadata_t *meta,
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t refresh_qos_queues(
                    _In_ const sai_attr_metadata_t *meta,
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t refresh_scheduler_groups(
                    _In_ const sai_attr_metadata_t *meta,
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t refresh_bridge_port_list(
                    _In_ const sai_attr_metadata_t *meta,
                    _In_ sai_object_id_t bridge_id);

            virtual sai_status_t refresh_vlan_member_list(
                    _In_ const sai_attr_metadata_t *meta,
                    _In_ sai_object_id_t vlan_id);

            virtual sai_status_t refresh_port_list(
                    _In_ const sai_attr_metadata_t *meta);

        public:

            virtual sai_status_t warm_boot_initialize_objects();

            virtual sai_status_t refresh_read_only(
                    _In_ const sai_attr_metadata_t *meta,
                    _In_ sai_object_id_t object_id);

        protected: // TODO should be pure

            virtual sai_status_t create_qos_queues_per_port(
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t create_qos_queues();

            virtual sai_status_t create_scheduler_group_tree(
                    _In_ const std::vector<sai_object_id_t>& sgs,
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t create_scheduler_groups_per_port(
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t create_scheduler_groups();

        protected: // will generate new OID

            virtual sai_status_t create(
                    _In_ sai_object_type_t object_type,
                    _Out_ sai_object_id_t *object_id,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            virtual sai_status_t remove(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id);

            virtual sai_status_t set(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId,
                    _In_ const sai_attribute_t* attr);

            virtual sai_status_t get(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _In_ uint32_t attr_count,
                    _Out_ sai_attribute_t *attr_list);

        public:

            virtual sai_status_t create(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::string &serializedObjectId,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            virtual sai_status_t remove(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::string &serializedObjectId);

            virtual sai_status_t set(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string &serializedObjectId,
                    _In_ const sai_attribute_t* attr);

            virtual sai_status_t get(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::string &serializedObjectId,
                    _In_ uint32_t attr_count,
                    _Out_ sai_attribute_t *attr_list);

        protected:

            virtual sai_status_t remove_internal(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::string &serializedObjectId);

            virtual sai_status_t create_internal(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::string &serializedObjectId,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            virtual sai_status_t set_internal(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string &serializedObjectId,
                    _In_ const sai_attribute_t* attr);

        private:

            sai_object_type_t objectTypeQuery(
                    _In_ sai_object_id_t objectId);

            sai_object_id_t switchIdQuery(
                    _In_ sai_object_id_t objectId);

        public:

            void processFdbEntriesForAging();

        private: // fdb related

            void updateLocalDB(
                    _In_ const sai_fdb_event_notification_data_t &data,
                    _In_ sai_fdb_event_t fdb_event);

            void processFdbInfo(
                    _In_ const FdbInfo &fi,
                    _In_ sai_fdb_event_t fdb_event);

            void findBridgeVlanForPortVlan(
                    _In_ sai_object_id_t port_id,
                    _In_ sai_vlan_id_t vlan_id,
                    _Inout_ sai_object_id_t &bv_id,
                    _Inout_ sai_object_id_t &bridge_port_id);

            bool getLagFromPort(
                    _In_ sai_object_id_t port_id,
                    _Inout_ sai_object_id_t& lag_id);

            bool isLagOrPortRifBased(
                    _In_ sai_object_id_t lag_or_port_id);

        public: 

            void process_packet_for_fdb_event(
                    _In_ sai_object_id_t portId,
                    _In_ const std::string& name,
                    _In_ const uint8_t *buffer,
                    _In_ size_t size);

            void debugSetStats(
                    _In_ sai_object_id_t oid,
                    _In_ const std::map<sai_stat_id_t, uint64_t>& stats);

        protected: // custom port

            sai_status_t createPort(
                    _In_ sai_object_id_t object_id,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            sai_status_t removePort(
                    _In_ sai_object_id_t objectId);

            sai_status_t setPort(
                    _In_ sai_object_id_t objectId,
                    _In_ const sai_attribute_t* attr);

            bool isPortReadyToBeRemove(
                    _In_ sai_object_id_t portId);

            std::vector<sai_object_id_t> getPortDependencies(
                    _In_ sai_object_id_t portId);

            sai_status_t check_port_dependencies(
                    _In_ sai_object_id_t port_id,
                    _Out_ std::vector<sai_object_id_t>& dep);

            bool check_port_reference_count(
                    _In_ sai_object_id_t port_id);

            bool get_object_list(
                    _In_ sai_object_id_t object_id,
                    _In_ sai_attr_id_t attr_id,
                    _Out_ std::vector<sai_object_id_t>& objlist);

            bool get_port_queues(
                    _In_ sai_object_id_t port_id,
                    _Out_ std::vector<sai_object_id_t>& queues);

            bool get_port_ipgs(
                    _In_ sai_object_id_t port_id,
                    _Out_ std::vector<sai_object_id_t>& ipgs);

            bool get_port_sg(
                    _In_ sai_object_id_t port_id,
                    _Out_ std::vector<sai_object_id_t>& sg);

            bool check_object_default_state(
                    _In_ sai_object_id_t object_id);

            bool check_object_list_default_state(
                    _Out_ const std::vector<sai_object_id_t>& objlist);

        protected: // custom debug counter

            uint32_t getNewDebugCounterIndex();

            void releaseDebugCounterIndex(
                    _In_ uint32_t index);

            sai_status_t createDebugCounter(
                    _In_ sai_object_id_t object_id,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            sai_status_t removeDebugCounter(
                    _In_ sai_object_id_t objectId);

        protected: // custom hostif

            sai_status_t createHostif(
                    _In_ sai_object_id_t object_id,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            sai_status_t removeHostif(
                    _In_ sai_object_id_t objectId);
            
            sai_status_t vs_remove_hostif_tap_interface(
                    _In_ sai_object_id_t hostif_id);

            sai_status_t vs_create_hostif_tap_interface(
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            bool hostif_create_tap_veth_forwarding(
                    _In_ const std::string &tapname,
                    _In_ int tapfd,
                    _In_ sai_object_id_t port_id);

            static int vs_create_tap_device(
                    _In_ const char *dev,
                    _In_ int flags);

            static int vs_set_dev_mac_address(
                    _In_ const char *dev,
                    _In_ const sai_mac_t& mac);

            static int promisc(
                    _In_ const char *dev);

            int vs_set_dev_mtu(
                    _In_ const char*name,
                    _In_ int mtu);

            int ifup(
                    _In_ const char *dev,
                    _In_ sai_object_id_t port_id,
                    _In_ bool up,
                    _In_ bool explicitNotification);

            std::string vs_get_veth_name(
                    _In_ const std::string& tapname,
                    _In_ sai_object_id_t port_id);

            void send_port_oper_status_notification(
                    _In_ sai_object_id_t port_id,
                    _In_ sai_port_oper_status_t status,
                    _In_ bool force);

            bool hasIfIndex(
                    _In_ int ifIndex) const;

        public: // TODO move inside warm boot load state

            sai_status_t vs_recreate_hostif_tap_interfaces();

            void update_port_oper_status(
                    _In_ sai_object_id_t port_id,
                    _In_ sai_port_oper_status_t port_oper_status);

            std::string dump_switch_database_for_warm_restart() const;

            void syncOnLinkMsg(
                    _In_ std::shared_ptr<EventPayloadNetLinkMsg> payload);

            void send_fdb_event_notification(
                    _In_ const sai_fdb_event_notification_data_t& data);

        protected:

            constexpr static const int maxDebugCounters = 32;

            std::unordered_set<uint32_t> m_indices;

        protected:

            std::vector<sai_object_id_t> m_port_list;
            std::vector<sai_object_id_t> m_bridge_port_list_port_based;

            std::vector<sai_acl_action_type_t> m_ingress_acl_action_list;
            std::vector<sai_acl_action_type_t> m_egress_acl_action_list;

            sai_object_id_t m_cpu_port_id;
            sai_object_id_t m_default_1q_bridge;
            sai_object_id_t m_default_bridge_port_1q_router;
            sai_object_id_t m_default_vlan_id;

        public: // TODO private

            std::set<FdbInfo> m_fdb_info_set;

            std::map<std::string, std::shared_ptr<HostInterfaceInfo>> m_hostif_info_map;

            std::shared_ptr<RealObjectIdManager> m_realObjectIdManager;
    };
}

