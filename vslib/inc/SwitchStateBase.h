
#pragma once

#include "SwitchState.h"

namespace saivs
{
    class SwitchStateBase:
        public SwitchState
    {
        public:

            SwitchStateBase(
                    _In_ sai_object_id_t switch_id);

            virtual ~SwitchStateBase() = default;

        public: // TODO to protected

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
                    _In_ sai_object_id_t switch_id,
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t create_ingress_priority_groups();

            virtual sai_status_t create_vlan_members();

            virtual sai_status_t create_bridge_ports();

            virtual sai_status_t set_acl_entry_min_prio();

            virtual sai_status_t set_acl_capabilities();

            virtual sai_status_t set_maximum_number_of_childs_per_scheduler_group();

            virtual sai_status_t set_number_of_ecmp_groups();

            virtual sai_status_t initialize_default_objects();

            virtual sai_status_t create_port(
                    _In_ sai_object_id_t port_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

        public: // TODO should be pure

            virtual sai_status_t create_qos_queues_per_port(
                    _In_ sai_object_id_t switch_object_id,
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t create_qos_queues();

            virtual sai_status_t create_scheduler_group_tree(
                    _In_ const std::vector<sai_object_id_t>& sgs,
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t create_scheduler_groups_per_port(
                    _In_ sai_object_id_t switch_id,
                    _In_ sai_object_id_t port_id);

            virtual sai_status_t create_scheduler_groups();

        protected:

            virtual sai_status_t create(
                    _In_ sai_object_type_t object_type,
                    _Out_ sai_object_id_t *object_id,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            virtual sai_status_t set(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId,
                    _In_ const sai_attribute_t* attr);

        public: // TODO to protected

            std::vector<sai_object_id_t> m_port_list;
            std::vector<sai_object_id_t> m_bridge_port_list_port_based;

            std::vector<sai_acl_action_type_t> m_ingress_acl_action_list;
            std::vector<sai_acl_action_type_t> m_egress_acl_action_list;

            sai_object_id_t m_cpu_port_id;
            sai_object_id_t m_default_1q_bridge;
            sai_object_id_t m_default_bridge_port_1q_router;
            sai_object_id_t m_default_vlan_id;

    };
}

