#include "SwitchStateBase.h"

#include "swss/logger.h"

#include "sai_vs.h" // TODO to be removed

using namespace saivs;

SwitchStateBase::SwitchStateBase(
                    _In_ sai_object_id_t switch_id):
    SwitchState(switch_id)
{
    SWSS_LOG_ENTER();

    // empty
}

sai_status_t SwitchStateBase::create(
        _In_ sai_object_type_t object_type,
        _Out_ sai_object_id_t *object_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return vs_generic_create(object_type, object_id, switch_id, attr_count, attr_list);
}

sai_status_t SwitchStateBase::set(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ const sai_attribute_t* attr)
{
    SWSS_LOG_ENTER();

    return vs_generic_set(objectType, objectId, attr);
}

sai_status_t SwitchStateBase::set_switch_mac_address()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create switch src mac address");

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_SRC_MAC_ADDRESS;

    attr.value.mac[0] = 0x11;
    attr.value.mac[1] = 0x22;
    attr.value.mac[2] = 0x33;
    attr.value.mac[3] = 0x44;
    attr.value.mac[4] = 0x55;
    attr.value.mac[5] = 0x66;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::set_switch_default_attributes()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create switch default attributes");

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY;
    attr.value.ptr = NULL;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    attr.id = SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    attr.id = SAI_SWITCH_ATTR_FDB_AGING_TIME;
    attr.value.u32 = 0;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    attr.id = SAI_SWITCH_ATTR_RESTART_WARM;
    attr.value.booldata = false;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    attr.id = SAI_SWITCH_ATTR_WARM_RECOVER;
    attr.value.booldata = false;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::create_default_vlan()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create default vlan");

    sai_attribute_t attr;

    attr.id = SAI_VLAN_ATTR_VLAN_ID;
    attr.value.u16 = DEFAULT_VLAN_NUMBER;

    CHECK_STATUS(create(SAI_OBJECT_TYPE_VLAN, &m_default_vlan_id, m_switch_id, 1, &attr));

    /* set default vlan on switch */

    attr.id = SAI_SWITCH_ATTR_DEFAULT_VLAN_ID;
    attr.value.oid = m_default_vlan_id;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::create_cpu_port()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create cpu port");

    sai_attribute_t attr;

    CHECK_STATUS(create(SAI_OBJECT_TYPE_PORT, &m_cpu_port_id, m_switch_id, 0, &attr));

    // populate cpu port object on switch
    attr.id = SAI_SWITCH_ATTR_CPU_PORT;
    attr.value.oid = m_cpu_port_id;

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    // set type on cpu
    attr.id = SAI_PORT_ATTR_TYPE;
    attr.value.s32 = SAI_PORT_TYPE_CPU;

    return set(SAI_OBJECT_TYPE_PORT, m_cpu_port_id, &attr);
}

sai_status_t SwitchStateBase::create_default_1q_bridge()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create default 1q bridge");

    sai_attribute_t attr;

    attr.id = SAI_BRIDGE_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_TYPE_1Q;

    CHECK_STATUS(create(SAI_OBJECT_TYPE_BRIDGE, &m_default_1q_bridge, m_switch_id, 1, &attr));

    attr.id = SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID;
    attr.value.oid = m_default_1q_bridge;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::create_ports()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create ports");

    std::vector<std::vector<uint32_t>> laneMap;

    getPortLaneMap(laneMap); // TODO per switch !! on constructor !

    uint32_t port_count = (uint32_t)laneMap.size();

    m_port_list.clear();

    for (uint32_t i = 0; i < port_count; i++)
    {
        SWSS_LOG_DEBUG("create port index %u", i);

        sai_object_id_t port_id;

        CHECK_STATUS(create(SAI_OBJECT_TYPE_PORT, &port_id, m_switch_id, 0, NULL));

        m_port_list.push_back(port_id);

        sai_attribute_t attr;

        attr.id = SAI_PORT_ATTR_ADMIN_STATE;
        attr.value.booldata = false;     /* default admin state is down as defined in SAI */

        CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

        attr.id = SAI_PORT_ATTR_MTU;
        attr.value.u32 = 1514;     /* default MTU is 1514 as defined in SAI */

        CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

        attr.id = SAI_PORT_ATTR_SPEED;
        attr.value.u32 = 40 * 1000;     /* TODO from config */

        CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

        std::vector<uint32_t> lanes = laneMap.at(i);

        attr.id = SAI_PORT_ATTR_HW_LANE_LIST;
        attr.value.u32list.count = (uint32_t)lanes.size();
        attr.value.u32list.list = lanes.data();

        CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

        attr.id = SAI_PORT_ATTR_TYPE;
        attr.value.s32 = SAI_PORT_TYPE_LOGICAL;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

        attr.id = SAI_PORT_ATTR_OPER_STATUS;
        attr.value.s32 = SAI_PORT_OPER_STATUS_DOWN;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

        attr.id = SAI_PORT_ATTR_PORT_VLAN_ID;
        attr.value.u32 = DEFAULT_VLAN_NUMBER;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchStateBase::set_port_list()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("set port list");

    /*
     * TODO this is static, when we start to "create/remove" ports we need to
     * update this list since it can change depends on profile.ini
     */

    sai_attribute_t attr;

    uint32_t port_count = (uint32_t)m_port_list.size();

    attr.id = SAI_SWITCH_ATTR_PORT_LIST;
    attr.value.objlist.count = port_count;
    attr.value.objlist.list = m_port_list.data();

    CHECK_STATUS(set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    attr.id = SAI_SWITCH_ATTR_PORT_NUMBER;
    attr.value.u32 = port_count;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::create_default_virtual_router()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create default virtual router");

    sai_object_id_t virtual_router_id;

    CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, &virtual_router_id, m_switch_id, 0, NULL));

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_DEFAULT_VIRTUAL_ROUTER_ID;
    attr.value.oid = virtual_router_id;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::create_default_stp_instance()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create default stp instance");

    sai_object_id_t stp_instance_id;

    CHECK_STATUS(create(SAI_OBJECT_TYPE_STP, &stp_instance_id, m_switch_id, 0, NULL));

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID;
    attr.value.oid = stp_instance_id;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::create_default_trap_group()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create default trap group");

    sai_object_id_t trap_group_id;

    CHECK_STATUS(create(SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP, &trap_group_id, m_switch_id, 0, NULL));

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP;
    attr.value.oid = trap_group_id;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::create_ingress_priority_groups_per_port(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    const uint32_t port_pgs_count = 8; // TODO must be per switch (mlnx and brcm is 8)

    std::vector<sai_object_id_t> pgs;

    for (uint32_t i = 0; i < port_pgs_count; ++i)
    {
        sai_object_id_t pg_id;

        sai_attribute_t attr[3];

        // TODO on brcm this attribute is not added

        attr[0].id = SAI_INGRESS_PRIORITY_GROUP_ATTR_BUFFER_PROFILE;
        attr[0].value.oid = SAI_NULL_OBJECT_ID;

        /*
         * not in headers yet
         *
         * attr[1].id = SAI_INGRESS_PRIORITY_GROUP_ATTR_PORT;
         * attr[1].value.oid = port_id;

         * attr[2].id = SAI_INGRESS_PRIORITY_GROUP_ATTR_INDEX;
         * attr[2].value.oid = i;

         * TODO fix number of attributes
         */

        CHECK_STATUS(create(SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP, &pg_id, switch_id, 1, attr));

        pgs.push_back(pg_id);
    }

    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS;
    attr.value.u32 = port_pgs_count;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    attr.id = SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST;
    attr.value.objlist.count = port_pgs_count;
    attr.value.objlist.list = pgs.data();

    CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchStateBase::create_ingress_priority_groups()
{
    SWSS_LOG_ENTER();

    // TODO priority groups size may change when we will modify pg or ports

    SWSS_LOG_INFO("create ingress priority groups");

    for (auto &port_id : m_port_list)
    {
        create_ingress_priority_groups_per_port(m_switch_id, port_id);
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchStateBase::create_vlan_members()
{
    SWSS_LOG_ENTER();

    // Crete vlan members for bridge ports.

    for (auto bridge_port_id: m_bridge_port_list_port_based)
    {
        SWSS_LOG_DEBUG("create vlan member for bridge port %s",
                sai_serialize_object_id(bridge_port_id).c_str());

        sai_attribute_t attrs[3];

        attrs[0].id = SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID;
        attrs[0].value.oid = bridge_port_id;

        attrs[1].id = SAI_VLAN_MEMBER_ATTR_VLAN_ID;
        attrs[1].value.oid = m_default_vlan_id;

        attrs[2].id = SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE;
        attrs[2].value.s32 = SAI_VLAN_TAGGING_MODE_UNTAGGED;

        sai_object_id_t vlan_member_id;

        CHECK_STATUS(create(SAI_OBJECT_TYPE_VLAN_MEMBER, &vlan_member_id, m_switch_id, 3, attrs));
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchStateBase::create_bridge_ports()
{
    SWSS_LOG_ENTER();

    // Create bridge port for 1q router.

    sai_attribute_t attr;

    attr.id = SAI_BRIDGE_PORT_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_PORT_TYPE_1Q_ROUTER;

    CHECK_STATUS(create(SAI_OBJECT_TYPE_BRIDGE_PORT, &m_default_bridge_port_1q_router, m_switch_id, 1, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    attr.value.oid = SAI_NULL_OBJECT_ID;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_BRIDGE_PORT, m_default_bridge_port_1q_router, &attr));

    // Create bridge ports for regular ports.

    m_bridge_port_list_port_based.clear();

    for (const auto &port_id: m_port_list)
    {
        SWSS_LOG_DEBUG("create bridge port for port %s", sai_serialize_object_id(port_id).c_str());

        sai_attribute_t attrs[4];

        attrs[0].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
        attrs[0].value.oid = m_default_1q_bridge;

        attrs[1].id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
        attrs[1].value.s32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW;

        attrs[2].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
        attrs[2].value.oid = port_id;

        attrs[3].id = SAI_BRIDGE_PORT_ATTR_TYPE;
        attrs[3].value.s32 = SAI_BRIDGE_PORT_TYPE_PORT;

        sai_object_id_t bridge_port_id;

        CHECK_STATUS(create(SAI_OBJECT_TYPE_BRIDGE_PORT, &bridge_port_id, m_switch_id, 4, attrs));

        m_bridge_port_list_port_based.push_back(bridge_port_id);
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchStateBase::set_acl_entry_min_prio()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("set acl entry min prio");

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY;
    attr.value.u32 = 1;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    attr.id = SAI_SWITCH_ATTR_ACL_ENTRY_MAXIMUM_PRIORITY;
    attr.value.u32 = 16000;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::set_acl_capabilities()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("set acl capabilities");

    sai_attribute_t attr;

    m_ingress_acl_action_list.clear();
    m_egress_acl_action_list.clear();

    for (int action_type = SAI_ACL_ENTRY_ATTR_ACTION_START; action_type <= SAI_ACL_ENTRY_ATTR_ACTION_END; action_type++)
    {
        m_ingress_acl_action_list.push_back(static_cast<sai_acl_action_type_t>(action_type - SAI_ACL_ENTRY_ATTR_ACTION_START));
        m_egress_acl_action_list.push_back(static_cast<sai_acl_action_type_t>(action_type - SAI_ACL_ENTRY_ATTR_ACTION_START));
    }

    attr.id = SAI_SWITCH_ATTR_MAX_ACL_ACTION_COUNT;
    attr.value.u32 = static_cast<uint32_t>(std::max(m_ingress_acl_action_list.size(), m_egress_acl_action_list.size()));

    CHECK_STATUS(set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    attr.id = SAI_SWITCH_ATTR_ACL_STAGE_INGRESS;
    attr.value.aclcapability.action_list.list = reinterpret_cast<int32_t*>(m_ingress_acl_action_list.data());
    attr.value.aclcapability.action_list.count = static_cast<uint32_t>(m_ingress_acl_action_list.size());

    CHECK_STATUS(set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr));

    attr.id = SAI_SWITCH_ATTR_ACL_STAGE_EGRESS;
    attr.value.aclcapability.action_list.list = reinterpret_cast<int32_t*>(m_egress_acl_action_list.data());
    attr.value.aclcapability.action_list.count = static_cast<uint32_t>(m_egress_acl_action_list.size());

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

sai_status_t SwitchStateBase::create_qos_queues_per_port(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    // TODO this method can be abstract, FIXME

    SWSS_LOG_ERROR("implement in child class");

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t SwitchStateBase::create_qos_queues()
{
    SWSS_LOG_ENTER();

    // TODO this method can be abstract, FIXME

    SWSS_LOG_ERROR("implement in child class");

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t SwitchStateBase::create_scheduler_group_tree(
        _In_ const std::vector<sai_object_id_t>& sgs,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    // TODO this method can be abstract, FIXME

    SWSS_LOG_ERROR("implement in child class");

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t SwitchStateBase::create_scheduler_groups_per_port(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    // TODO this method can be abstract, FIXME

    SWSS_LOG_ERROR("implement in child class");

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t SwitchStateBase::create_scheduler_groups()
{
    SWSS_LOG_ENTER();

    // TODO this method can be abstract, FIXME

    SWSS_LOG_ERROR("implement in child class");

    return SAI_STATUS_NOT_IMPLEMENTED;
}
