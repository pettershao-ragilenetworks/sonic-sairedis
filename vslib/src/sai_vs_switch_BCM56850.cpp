#include "sai_vs.h"
#include <net/if.h>
#include <algorithm>

#include "SwitchStateBase.h"

using namespace saivs;

// TODO extra work may be needed on GET api if N on list will be > then actual

/*
 * We can use local variable here for initialization (init should be in class
 * constructor anyway, we can move it there later) because each switch init is
 * done under global lock.
 */

static std::shared_ptr<SwitchStateBase> ss;

static std::vector<sai_object_id_t> bridge_port_list_port_based;

static std::vector<sai_acl_action_type_t> ingress_acl_action_list;
static std::vector<sai_acl_action_type_t> egress_acl_action_list;

static sai_status_t create_bridge_ports()
{
    SWSS_LOG_ENTER();

    sai_object_id_t switch_id = ss->getSwitchId();

    /*
     * Create bridge port for 1q router.
     */

    sai_attribute_t attr;

    attr.id = SAI_BRIDGE_PORT_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_PORT_TYPE_1Q_ROUTER;

    sai_object_id_t default_bridge_port_1q_router;

    CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_BRIDGE_PORT, &default_bridge_port_1q_router, ss->getSwitchId(), 1, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    attr.value.oid = SAI_NULL_OBJECT_ID;

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_BRIDGE_PORT, default_bridge_port_1q_router, &attr));

    attr.id = SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID;

    CHECK_STATUS(vs_generic_get(SAI_OBJECT_TYPE_SWITCH, switch_id, 1, &attr));

    /*
     * Create bridge ports for regular ports.
     */

    sai_object_id_t default_1q_bridge_id = attr.value.oid;

    bridge_port_list_port_based.clear();

    for (const auto &port_id: ss->m_port_list)
    {
        SWSS_LOG_DEBUG("create bridge port for port %s", sai_serialize_object_id(port_id).c_str());

        sai_attribute_t attrs[4];

        attrs[0].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
        attrs[0].value.oid = default_1q_bridge_id;

        attrs[1].id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
        attrs[1].value.s32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW;

        attrs[2].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
        attrs[2].value.oid = port_id;

        attrs[3].id = SAI_BRIDGE_PORT_ATTR_TYPE;
        attrs[3].value.s32 = SAI_BRIDGE_PORT_TYPE_PORT;

        sai_object_id_t bridge_port_id;

        CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_BRIDGE_PORT, &bridge_port_id, switch_id, 4, attrs));

        bridge_port_list_port_based.push_back(bridge_port_id);
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t create_default_stp_instance()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create default stp instance");

    sai_object_id_t stp_instance_id;

    sai_object_id_t switch_object_id = ss->getSwitchId();

    CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_STP, &stp_instance_id, switch_object_id, 0, NULL));

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID;
    attr.value.oid = stp_instance_id;

    return vs_generic_set(SAI_OBJECT_TYPE_SWITCH, switch_object_id, &attr);
}

/*
static sai_status_t create_vlan_members_for_default_vlan(
        std::vector<sai_object_id_t>& bridge_port_list,
        std::vector<sai_object_id_t>& vlan_member_list)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create vlan members for all ports");

    sai_object_id_t switch_object_id = ss->getSwitchId();

    vlan_member_list.clear();

    ss->vlan_members_map[ss->default_vlan_id] = {};

    for (auto &bridge_port_id : bridge_port_list)
    {
        sai_object_id_t vlan_member_id;

        std::vector<sai_attribute_t> attrs;

        sai_attribute_t attr_vlan_id;

        attr_vlan_id.id = SAI_VLAN_MEMBER_ATTR_VLAN_ID;
        attr_vlan_id.value.u16 = DEFAULT_VLAN_NUMBER;

        attrs.push_back(attr_vlan_id);

        sai_attribute_t attr_port_id;

        // TODO first we need to create bridge and bridge ports ?

        attr_port_id.id = SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID;
        attr_port_id.value.oid = bridge_port_id;

        attrs.push_back(attr_port_id);

        CHECK_STATUS(vs_generic_create(
                    SAI_OBJECT_TYPE_VLAN_MEMBER,
                    &vlan_member_id,
                    switch_object_id,
                    (uint32_t)attrs.size(),
                    attrs.data()));

        vlan_member_list.push_back(vlan_member_id);

        ss->vlan_members_map[ss->default_vlan_id].insert(vlan_member_id);
    }

    return SAI_STATUS_SUCCESS;
}
*/

static sai_status_t create_default_trap_group()
{
    SWSS_LOG_ENTER();

    sai_object_id_t switch_object_id = ss->getSwitchId();

    SWSS_LOG_INFO("create default trap group");

    sai_object_id_t trap_group_id;

    CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP, &trap_group_id, switch_object_id, 0, NULL));

    sai_attribute_t attr;

    // populate trap group on switch
    attr.id = SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP;
    attr.value.oid = trap_group_id;

    return vs_generic_set(SAI_OBJECT_TYPE_SWITCH, switch_object_id, &attr);
}

static sai_status_t create_qos_queues_per_port(
        _In_ sai_object_id_t switch_object_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    sai_attribute_t attr;

    // 10 in and 10 out queues per port
    const uint32_t port_qos_queues_count = 20;

    std::vector<sai_object_id_t> queues;

    for (uint32_t i = 0; i < port_qos_queues_count; ++i)
    {
        sai_object_id_t queue_id;

        CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_QUEUE, &queue_id, switch_object_id, 0, NULL));

        queues.push_back(queue_id);

        attr.id = SAI_QUEUE_ATTR_TYPE;
        attr.value.s32 = (i < port_qos_queues_count / 2) ?  SAI_QUEUE_TYPE_UNICAST : SAI_QUEUE_TYPE_MULTICAST;

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_QUEUE, queue_id, &attr));

        attr.id = SAI_QUEUE_ATTR_INDEX;
        attr.value.u8 = (uint8_t)i;

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_QUEUE, queue_id, &attr));
    }

    attr.id = SAI_PORT_ATTR_QOS_NUMBER_OF_QUEUES;
    attr.value.u32 = port_qos_queues_count;

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    attr.id = SAI_PORT_ATTR_QOS_QUEUE_LIST;
    attr.value.objlist.count = port_qos_queues_count;
    attr.value.objlist.list = queues.data();

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    return SAI_STATUS_SUCCESS;
}

static sai_status_t create_qos_queues()
{
    SWSS_LOG_ENTER();

    // TODO queues size may change when we will modify queue or ports

    SWSS_LOG_INFO("create qos queues");

    sai_object_id_t switch_object_id = ss->getSwitchId();

    for (auto &port_id : ss->m_port_list)
    {
        CHECK_STATUS(create_qos_queues_per_port(switch_object_id, port_id));
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t create_ingress_priority_groups_per_port(
        _In_ sai_object_id_t switch_object_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    const uint32_t port_pgs_count = 8;

    std::vector<sai_object_id_t> pgs;

    for (uint32_t i = 0; i < port_pgs_count; ++i)
    {
        sai_object_id_t pg_id;

        CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP, &pg_id, switch_object_id, 0, NULL));

        pgs.push_back(pg_id);
    }

    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS;
    attr.value.u32 = port_pgs_count;

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    attr.id = SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST;
    attr.value.objlist.count = port_pgs_count;
    attr.value.objlist.list = pgs.data();

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    return SAI_STATUS_SUCCESS;
}

static sai_status_t create_ingress_priority_groups()
{
    SWSS_LOG_ENTER();

    // TODO priority groups size may change when we will modify pg or ports

    SWSS_LOG_INFO("create priority groups");

    sai_object_id_t switch_object_id = ss->getSwitchId();

    for (auto &port_id : ss->m_port_list)
    {
        CHECK_STATUS(create_ingress_priority_groups_per_port(switch_object_id, port_id));
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t create_scheduler_group_tree(
        _In_ const std::vector<sai_object_id_t>& sgs,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    sai_attribute_t attrq;

    std::vector<sai_object_id_t> queues;

    // in this implementation we have 20 queues per port
    // (10 in and 10 out), which will be assigned to schedulers
    uint32_t queues_count = 20;

    queues.resize(queues_count);

    attrq.id = SAI_PORT_ATTR_QOS_QUEUE_LIST;
    attrq.value.objlist.count = queues_count;
    attrq.value.objlist.list = queues.data();

    // NOTE it will do recalculate
    CHECK_STATUS(vs_generic_get(SAI_OBJECT_TYPE_PORT, port_id, 1, &attrq));

    // schedulers groups: 0 1 2 3 4 5 6 7 8 9 a b c

    // tree index
    // 0 = 1 2
    // 1 = 3 4 5 6 7 8 9 a
    // 2 = b c (bug on brcm)

    // 3..c - have both QUEUES, each one 2

    // scheduler group 0 (2 groups)
    {
        sai_object_id_t sg_0 = sgs.at(0);

        sai_attribute_t attr;

        attr.id = SAI_SCHEDULER_GROUP_ATTR_PORT_ID;
        attr.value.oid = port_id;

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_0, &attr));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
        attr.value.u32 = 2;

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_0, &attr));

        uint32_t list_count = 2;
        std::vector<sai_object_id_t> list;

        list.push_back(sgs.at(1));
        list.push_back(sgs.at(2));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
        attr.value.objlist.count = list_count;
        attr.value.objlist.list = list.data();

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_0, &attr));
    }

    uint32_t queue_index = 0;

    // scheduler group 1 (8 groups)
    {
        sai_object_id_t sg_1 = sgs.at(1);

        sai_attribute_t attr;

        attr.id = SAI_SCHEDULER_GROUP_ATTR_PORT_ID;
        attr.value.oid = port_id;

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_1, &attr));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
        attr.value.u32 = 8;

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_1, &attr));

        uint32_t list_count = 8;
        std::vector<sai_object_id_t> list;

        list.push_back(sgs.at(3));
        list.push_back(sgs.at(4));
        list.push_back(sgs.at(5));
        list.push_back(sgs.at(6));
        list.push_back(sgs.at(7));
        list.push_back(sgs.at(8));
        list.push_back(sgs.at(9));
        list.push_back(sgs.at(0xa));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
        attr.value.objlist.count = list_count;
        attr.value.objlist.list = list.data();

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_1, &attr));

        // now assign queues to level 1 scheduler groups,

        for (size_t i = 0; i < list.size(); ++i)
        {
            sai_object_id_t childs[2];

            childs[0] = queues[queue_index];    // first half are in queues
            childs[1] = queues[queue_index + queues_count/2]; // second half are out queues

            // for each scheduler set 2 queues
            attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
            attr.value.objlist.count = 2;
            attr.value.objlist.list = childs;

            queue_index++;

            CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, list.at(i), &attr));

            attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
            attr.value.u32 = 2;

            CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, list.at(i), &attr));
        }
    }

    // scheduler group 2 (2 groups)
    {
        sai_object_id_t sg_2 = sgs.at(2);

        sai_attribute_t attr;

        attr.id = SAI_SCHEDULER_GROUP_ATTR_PORT_ID;
        attr.value.oid = port_id;

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_2, &attr));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
        attr.value.u32 = 2;

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_2, &attr));

        uint32_t list_count = 2;
        std::vector<sai_object_id_t> list;

        list.push_back(sgs.at(0xb));
        list.push_back(sgs.at(0xc));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
        attr.value.objlist.count = list_count;
        attr.value.objlist.list = list.data();

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_2, &attr));

        for (size_t i = 0; i < list.size(); ++i)
        {
            sai_object_id_t childs[2];

            // for each scheduler set 2 queues
            childs[0] = queues[queue_index];    // first half are in queues
            childs[1] = queues[queue_index + queues_count/2]; // second half are out queues

            attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
            attr.value.objlist.count = 2;
            attr.value.objlist.list = childs;

            queue_index++;

            CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, list.at(i), &attr));

            attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
            attr.value.u32 = 2;

            CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, list.at(i), &attr));
        }
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t create_scheduler_groups_per_port(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    uint32_t port_sgs_count = 13; // brcm default

    // NOTE: this is only static data, to keep track of this
    // we would need to create actual objects and keep them
    // in respected objects, we need to move in to that
    // solution when we will start using different "profiles"
    // currently this is good enough

    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_QOS_NUMBER_OF_SCHEDULER_GROUPS;
    attr.value.u32 = port_sgs_count;

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    // scheduler groups per port

    std::vector<sai_object_id_t> sgs;

    for (uint32_t i = 0; i < port_sgs_count; ++i)
    {
        sai_object_id_t sg_id;

        CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_SCHEDULER_GROUP, &sg_id, ss->getSwitchId(), 0, NULL));

        sgs.push_back(sg_id);
    }

    attr.id = SAI_PORT_ATTR_QOS_SCHEDULER_GROUP_LIST;
    attr.value.objlist.count = port_sgs_count;
    attr.value.objlist.list = sgs.data();

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    CHECK_STATUS(create_scheduler_group_tree(sgs, port_id));

    // TODO
    // SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT // sched_groups + count
    // scheduler group are organized in tree and on the bottom there are queues
    // order matters in returning api

    return SAI_STATUS_SUCCESS;
}

static sai_status_t create_scheduler_groups()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create scheduler groups");

    // TODO scheduler groups size may change when we will modify sg or ports

    sai_object_id_t switch_object_id = ss->getSwitchId();

    for (auto &port_id : ss->m_port_list)
    {
        CHECK_STATUS(create_scheduler_groups_per_port(switch_object_id, port_id));
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t set_maximum_number_of_childs_per_scheduler_group()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create switch src mac address");

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_CHILDS_PER_SCHEDULER_GROUP;
    attr.value.u32 = 16;

    return vs_generic_set(SAI_OBJECT_TYPE_SWITCH, ss->getSwitchId(), &attr);
}

static sai_status_t create_vlan_members()
{
    SWSS_LOG_ENTER();

    sai_object_id_t switch_id = ss->getSwitchId();

    /*
     * Crete vlan members for bridge ports.
     */

    for (auto bridge_port_id: bridge_port_list_port_based)
    {
        SWSS_LOG_DEBUG("create vlan member for bridge port %s",
                sai_serialize_object_id(bridge_port_id).c_str());

        sai_attribute_t attrs[3];

        attrs[0].id = SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID;
        attrs[0].value.oid = bridge_port_id;

        attrs[1].id = SAI_VLAN_MEMBER_ATTR_VLAN_ID;
        attrs[1].value.oid = ss->m_default_vlan_id;

        attrs[2].id = SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE;
        attrs[2].value.s32 = SAI_VLAN_TAGGING_MODE_UNTAGGED;

        sai_object_id_t vlan_member_id;

        CHECK_STATUS(vs_generic_create(SAI_OBJECT_TYPE_VLAN_MEMBER, &vlan_member_id, switch_id, 3, attrs));
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t create_acl_entry_min_prio()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create acl entry min prio");

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY;
    attr.value.u32 = 1;

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SWITCH, ss->getSwitchId(), &attr));

    attr.id = SAI_SWITCH_ATTR_ACL_ENTRY_MAXIMUM_PRIORITY;
    attr.value.u32 = 16000;

    return vs_generic_set(SAI_OBJECT_TYPE_SWITCH, ss->getSwitchId(), &attr);
}

static sai_status_t create_acl_capabilities()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create acl capabilities");

    sai_attribute_t attr;

    for (int action_type = SAI_ACL_ENTRY_ATTR_ACTION_START; action_type <= SAI_ACL_ENTRY_ATTR_ACTION_END; action_type++)
    {
        ingress_acl_action_list.push_back(static_cast<sai_acl_action_type_t>(action_type - SAI_ACL_ENTRY_ATTR_ACTION_START));
        egress_acl_action_list.push_back(static_cast<sai_acl_action_type_t>(action_type - SAI_ACL_ENTRY_ATTR_ACTION_START));
    }

    attr.id = SAI_SWITCH_ATTR_MAX_ACL_ACTION_COUNT;
    attr.value.u32 = static_cast<uint32_t>(std::max(ingress_acl_action_list.size(), egress_acl_action_list.size()));
    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SWITCH, ss->getSwitchId(), &attr));

    attr.id = SAI_SWITCH_ATTR_ACL_STAGE_INGRESS;
    attr.value.aclcapability.action_list.list = reinterpret_cast<int32_t*>(ingress_acl_action_list.data());
    attr.value.aclcapability.action_list.count = static_cast<uint32_t>(ingress_acl_action_list.size());

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SWITCH, ss->getSwitchId(), &attr));

    attr.id = SAI_SWITCH_ATTR_ACL_STAGE_EGRESS;
    attr.value.aclcapability.action_list.list = reinterpret_cast<int32_t*>(egress_acl_action_list.data());
    attr.value.aclcapability.action_list.count = static_cast<uint32_t>(egress_acl_action_list.size());

    return vs_generic_set(SAI_OBJECT_TYPE_SWITCH, ss->getSwitchId(), &attr);
}

static sai_status_t set_number_of_ecmp_groups()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("set number of ecmp groups");

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_NUMBER_OF_ECMP_GROUPS;
    attr.value.u32 = 512;

    return vs_generic_set(SAI_OBJECT_TYPE_SWITCH, ss->getSwitchId(), &attr);
}

static sai_status_t initialize_default_objects()
{
    SWSS_LOG_ENTER();

    CHECK_STATUS(ss->set_switch_mac_address());

    CHECK_STATUS(ss->create_cpu_port());
    CHECK_STATUS(ss->create_default_vlan());
    CHECK_STATUS(ss->create_default_virtual_router());
    CHECK_STATUS(create_default_stp_instance());
    CHECK_STATUS(ss->create_default_1q_bridge());
    CHECK_STATUS(create_default_trap_group());
    CHECK_STATUS(ss->create_ports());
    CHECK_STATUS(ss->set_port_list());
    CHECK_STATUS(create_bridge_ports());
    CHECK_STATUS(create_vlan_members());
    CHECK_STATUS(create_acl_entry_min_prio());
    CHECK_STATUS(create_acl_capabilities());
    CHECK_STATUS(create_ingress_priority_groups());
    CHECK_STATUS(create_qos_queues());
    CHECK_STATUS(set_maximum_number_of_childs_per_scheduler_group());
    CHECK_STATUS(set_number_of_ecmp_groups());
    CHECK_STATUS(ss->set_switch_default_attributes());
    CHECK_STATUS(create_scheduler_groups());

    return SAI_STATUS_SUCCESS;
}

static sai_status_t warm_boot_initialize_objects()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("warm boot init objects");

    /*
     * We need to bring back previous state in case user will get some read
     * only attributes and recalculation will need to be done.
     *
     * We only need to refresh ports since only ports are used in recalculation
     * logic.
     */

    sai_object_id_t switch_id = ss->getSwitchId();

    ss->m_port_list.resize(SAI_VS_MAX_PORTS);

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_PORT_LIST;

    attr.value.objlist.count = SAI_VS_MAX_PORTS;
    attr.value.objlist.list = ss->m_port_list.data();

    CHECK_STATUS(vs_generic_get(SAI_OBJECT_TYPE_SWITCH, switch_id, 1, &attr));

    ss->m_port_list.resize(attr.value.objlist.count);

    SWSS_LOG_NOTICE("port list size: %zu", ss->m_port_list.size());

    return SAI_STATUS_SUCCESS;
}

void init_switch_BCM56850(
        _In_ sai_object_id_t switch_id,
        _In_ std::shared_ptr<SwitchState> warmBootState)
{
    SWSS_LOG_ENTER();

    if (switch_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_THROW("init switch with NULL switch id is not allowed");
    }

    if (warmBootState != nullptr)
    {
        g_switch_state_map[switch_id] = warmBootState;

        ss = std::dynamic_pointer_cast<SwitchStateBase>(g_switch_state_map[switch_id]);

        warm_boot_initialize_objects();

        SWSS_LOG_NOTICE("initialized switch %s in WARM boot mode", sai_serialize_object_id(switch_id).c_str());

        return;
    }

    if (g_switch_state_map.find(switch_id) != g_switch_state_map.end())
    {
        SWSS_LOG_THROW("switch already exists 0x%lx", switch_id);
    }

    g_switch_state_map[switch_id] = std::make_shared<SwitchStateBase>(switch_id);

    ss = std::dynamic_pointer_cast<SwitchStateBase>(g_switch_state_map[switch_id]);

    sai_status_t status = initialize_default_objects();

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_THROW("unable to init switch %s", sai_serialize_status(status).c_str());
    }

    SWSS_LOG_NOTICE("initialized switch %s", sai_serialize_object_id(switch_id).c_str());
}

void uninit_switch_BCM56850(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    if (g_switch_state_map.find(switch_id) == g_switch_state_map.end())
    {
        SWSS_LOG_THROW("switch don't exists 0x%lx", switch_id);
    }

    SWSS_LOG_NOTICE("remove switch 0x%lx", switch_id);

    g_switch_state_map.erase(switch_id);
}

static sai_status_t refresh_bridge_port_list(
        _In_ const sai_attr_metadata_t *meta,
        _In_ sai_object_id_t bridge_id,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    /*
     * TODO possible issues with vxlan and lag.
     */

    auto &all_bridge_ports = ss->m_objectHash.at(SAI_OBJECT_TYPE_BRIDGE_PORT);

    sai_attribute_t attr;

    auto m_port_list = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_BRIDGE, SAI_BRIDGE_ATTR_PORT_LIST);
    auto m_port_id = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_BRIDGE_PORT, SAI_BRIDGE_PORT_ATTR_PORT_ID);
    auto m_bridge_id = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_BRIDGE_PORT, SAI_BRIDGE_PORT_ATTR_BRIDGE_ID);
    auto m_type = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_BRIDGE_PORT, SAI_BRIDGE_PORT_ATTR_TYPE);

    /*
     * First get all port's that belong to this bridge id.
     */

    attr.id = SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID;

    CHECK_STATUS(vs_generic_get(SAI_OBJECT_TYPE_SWITCH, switch_id, 1, &attr));

    /*
     * Create bridge ports for regular ports.
     */

    sai_object_id_t default_1q_bridge_id = attr.value.oid;

    std::map<sai_object_id_t, SwitchState::AttrHash> bridge_port_list_on_bridge_id;

    // update default bridge port id's for bridge port if attr type is missing
    for (const auto &bp: all_bridge_ports)
    {
        auto it = bp.second.find(m_type->attridname);

        if (it == bp.second.end())
            continue;

        if (it->second->getAttr()->value.s32 != SAI_BRIDGE_PORT_TYPE_PORT)
            continue;

        it = bp.second.find(m_bridge_id->attridname);

        if (it != bp.second.end())
            continue;

        // this bridge port is type PORT, and it's missing BRIDGE_ID attr

        SWSS_LOG_NOTICE("setting default bridge id (%s) on bridge port %s",
                sai_serialize_object_id(default_1q_bridge_id).c_str(),
                bp.first.c_str());

        attr.id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
        attr.value.oid = default_1q_bridge_id;

        sai_object_id_t bridge_port;
        sai_deserialize_object_id(bp.first, bridge_port);

        CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_BRIDGE_PORT, bridge_port, &attr));
    }

    // will contain 1q router bridge port, which we want to skip?
    for (const auto &bp: all_bridge_ports)
    {
        auto it = bp.second.find(m_bridge_id->attridname);

        if (it == bp.second.end())
        {
            // fine on router 1q
            SWSS_LOG_NOTICE("not found %s on bridge port: %s", m_bridge_id->attridname, bp.first.c_str());
            continue;
        }

        if (bridge_id == it->second->getAttr()->value.oid)
        {
            /*
             * This bridge port belongs to currently processing bridge ID.
             */

            sai_object_id_t bridge_port;

            sai_deserialize_object_id(bp.first, bridge_port);

            bridge_port_list_on_bridge_id[bridge_port] = bp.second;
        }
    }

    /*
     * Now sort those bridge port id's by port id to be consistent.
     */

    std::vector<sai_object_id_t> bridge_port_list;

    for (const auto &p: ss->m_port_list)
    {
        for (const auto &bp: bridge_port_list_on_bridge_id)
        {
            auto it = bp.second.find(m_port_id->attridname);

            if (it == bp.second.end())
            {
                SWSS_LOG_THROW("bridge port is missing %s, not supported yet, FIXME", m_port_id->attridname);
            }

            if (p == it->second->getAttr()->value.oid)
            {
                bridge_port_list.push_back(bp.first);
            }
        }
    }

    if (bridge_port_list_on_bridge_id.size() != bridge_port_list.size())
    {
        SWSS_LOG_THROW("filter by port id failed size on lists is different: %zu vs %zu",
                bridge_port_list_on_bridge_id.size(),
                bridge_port_list.size());
    }

    uint32_t bridge_port_list_count = (uint32_t)bridge_port_list.size();

    SWSS_LOG_NOTICE("recalculated %s: %u", m_port_list->attridname, bridge_port_list_count);

    attr.id = SAI_BRIDGE_ATTR_PORT_LIST;
    attr.value.objlist.count = bridge_port_list_count;
    attr.value.objlist.list = bridge_port_list.data();

    return vs_generic_set(SAI_OBJECT_TYPE_BRIDGE, bridge_id, &attr);
}

static sai_status_t refresh_vlan_member_list(
        _In_ const sai_attr_metadata_t *meta,
        _In_ sai_object_id_t vlan_id,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    auto &all_vlan_members = ss->m_objectHash.at(SAI_OBJECT_TYPE_VLAN_MEMBER);

    auto m_member_list = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_VLAN, SAI_VLAN_ATTR_MEMBER_LIST);
    auto md_vlan_id = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_VLAN_MEMBER, SAI_VLAN_MEMBER_ATTR_VLAN_ID);
    //auto md_brportid = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_VLAN_MEMBER, SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID);

    std::vector<sai_object_id_t> vlan_member_list;

    /*
     * We want order as bridge port order (so port order)
     */

    sai_attribute_t attr;

    auto me = ss->m_objectHash.at(SAI_OBJECT_TYPE_VLAN).at(sai_serialize_object_id(vlan_id));

    for (auto vm: all_vlan_members)
    {
        if (vm.second.at(md_vlan_id->attridname)->getAttr()->value.oid != vlan_id)
        {
            /*
             * Only interested in our vlan
             */

            continue;
        }

        // TODO we need order as bridge ports, but we need bridge id!

        {
            sai_object_id_t vlan_member_id;

            sai_deserialize_object_id(vm.first, vlan_member_id);

            vlan_member_list.push_back(vlan_member_id);
        }
    }

    uint32_t vlan_member_list_count = (uint32_t)vlan_member_list.size();

    SWSS_LOG_NOTICE("recalculated %s: %u", m_member_list->attridname, vlan_member_list_count);

    attr.id = SAI_VLAN_ATTR_MEMBER_LIST;
    attr.value.objlist.count = vlan_member_list_count;
    attr.value.objlist.list = vlan_member_list.data();

    return vs_generic_set(SAI_OBJECT_TYPE_VLAN, vlan_id, &attr);
}

static sai_status_t refresh_ingress_priority_group(
        _In_ const sai_attr_metadata_t *meta,
        _In_ sai_object_id_t port_id,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    /*
     * TODO Currently we don't have index in groups, so we don't know how to
     * sort.  Returning success, since assuming that we will not create more
     * ingress priority groups.
     */

    return SAI_STATUS_SUCCESS;
}

static sai_status_t refresh_qos_queues(
        _In_ const sai_attr_metadata_t *meta,
        _In_ sai_object_id_t port_id,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    /*
     * TODO Currently we don't have index in groups, so we don't know how to
     * sort.  Returning success, since assuming that we will not create more
     * ingress priority groups.
     */

    return SAI_STATUS_SUCCESS;
}

static sai_status_t refresh_scheduler_groups(
        _In_ const sai_attr_metadata_t *meta,
        _In_ sai_object_id_t port_id,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    /*
     * TODO Currently we don't have index in groups, so we don't know how to
     * sort.  Returning success, since assuming that we will not create more
     * ingress priority groups.
     */

    return SAI_STATUS_SUCCESS;
}

static sai_status_t refresh_port_list(
        _In_ const sai_attr_metadata_t *meta,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    // since now port can be added or removed, we need to update port list
    // dynamically

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_CPU_PORT;

    CHECK_STATUS(vs_generic_get(SAI_OBJECT_TYPE_SWITCH, switch_id, 1, &attr));

    const sai_object_id_t cpu_port_id = attr.value.oid;

    ss->m_port_list.clear();

    // iterate via ASIC state to find all the ports

    auto &m_objectHash = ss->m_objectHash.at(SAI_OBJECT_TYPE_PORT);

    for (const auto& it: m_objectHash)
    {
        sai_object_id_t port_id;
        sai_deserialize_object_id(it.first, port_id);

        // don't put CPU port id on the list

        if (port_id == cpu_port_id)
            continue;

        ss->m_port_list.push_back(port_id);
    }

    /*
     * TODO:
     *
     * Currently we don't know what's happen on brcm SAI implementation when
     * port is removed and then added, will new port could get the same vendor
     * OID or always different, and what is order of those new oids on the
     * PORT_LIST attribute.
     *
     * This needs to be investigated, and to reflect exact behaviour here.
     * Currently we just sort all the port oids.
     */

    std::sort(ss->m_port_list.begin(), ss->m_port_list.end());

    sai_object_id_t switch_object_id = ss->getSwitchId();

    uint32_t port_count = (uint32_t)ss->m_port_list.size();

    attr.id = SAI_SWITCH_ATTR_PORT_LIST;
    attr.value.objlist.count = port_count;
    attr.value.objlist.list = ss->m_port_list.data();

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SWITCH, switch_object_id, &attr));

    attr.id = SAI_SWITCH_ATTR_PORT_NUMBER;
    attr.value.u32 = port_count;

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_SWITCH, switch_object_id, &attr));

    SWSS_LOG_NOTICE("refreshed port list, current port number: %zu, not counting cpu port", ss->m_port_list.size());

    return SAI_STATUS_SUCCESS;
}

/*
 * NOTE For recalculation we can add flag on create/remove specific object type
 * so we can deduce whether actually need to perform recalculation, as
 * optimization.
 */

sai_status_t refresh_read_only_BCM56850(
        _In_ const sai_attr_metadata_t *meta,
        _In_ sai_object_id_t object_id,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    if (meta->objecttype == SAI_OBJECT_TYPE_SWITCH)
    {
        switch (meta->attrid)
        {
            case SAI_SWITCH_ATTR_CPU_PORT:
            case SAI_SWITCH_ATTR_DEFAULT_VIRTUAL_ROUTER_ID:
            case SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP:
            case SAI_SWITCH_ATTR_DEFAULT_VLAN_ID:
            case SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID:
            case SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID:
                return SAI_STATUS_SUCCESS;

            case SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY:
            case SAI_SWITCH_ATTR_ACL_ENTRY_MAXIMUM_PRIORITY:
                return SAI_STATUS_SUCCESS;

            case SAI_SWITCH_ATTR_MAX_ACL_ACTION_COUNT:
            case SAI_SWITCH_ATTR_ACL_STAGE_INGRESS:
            case SAI_SWITCH_ATTR_ACL_STAGE_EGRESS:
                return SAI_STATUS_SUCCESS;

            case SAI_SWITCH_ATTR_NUMBER_OF_ECMP_GROUPS:
                return SAI_STATUS_SUCCESS;

            case SAI_SWITCH_ATTR_PORT_NUMBER:
            case SAI_SWITCH_ATTR_PORT_LIST:
                return refresh_port_list(meta, switch_id);

            case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_CHILDS_PER_SCHEDULER_GROUP:
                return SAI_STATUS_SUCCESS;

            case SAI_SWITCH_ATTR_AVAILABLE_SNAT_ENTRY:
            case SAI_SWITCH_ATTR_AVAILABLE_DNAT_ENTRY:
            case SAI_SWITCH_ATTR_AVAILABLE_DOUBLE_NAT_ENTRY:
                return SAI_STATUS_SUCCESS;
        }
    }

    if (meta->objecttype == SAI_OBJECT_TYPE_PORT)
    {
        switch (meta->attrid)
        {
            case SAI_PORT_ATTR_QOS_NUMBER_OF_QUEUES:
            case SAI_PORT_ATTR_QOS_QUEUE_LIST:
                return refresh_qos_queues(meta, object_id, switch_id);

            case SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS:
            case SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST:
                return refresh_ingress_priority_group(meta, object_id, switch_id);

            case SAI_PORT_ATTR_QOS_NUMBER_OF_SCHEDULER_GROUPS:
            case SAI_PORT_ATTR_QOS_SCHEDULER_GROUP_LIST:
                return refresh_scheduler_groups(meta, object_id, switch_id);

                /*
                 * This status is based on hostif vEthernetX status.
                 */

            case SAI_PORT_ATTR_OPER_STATUS:
                return SAI_STATUS_SUCCESS;
        }
    }

    if (meta->objecttype == SAI_OBJECT_TYPE_SCHEDULER_GROUP)
    {
        switch (meta->attrid)
        {
            case SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT:
            case SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST:
                return refresh_scheduler_groups(meta, object_id, switch_id);
        }
    }

    if (meta->objecttype == SAI_OBJECT_TYPE_BRIDGE && meta->attrid == SAI_BRIDGE_ATTR_PORT_LIST)
    {
        return refresh_bridge_port_list(meta, object_id, switch_id);
    }

    if (meta->objecttype == SAI_OBJECT_TYPE_VLAN && meta->attrid == SAI_VLAN_ATTR_MEMBER_LIST)
    {
        return refresh_vlan_member_list(meta, object_id, switch_id);
    }

    if (meta->objecttype == SAI_OBJECT_TYPE_DEBUG_COUNTER && meta->attrid == SAI_DEBUG_COUNTER_ATTR_INDEX)
    {
        return SAI_STATUS_SUCCESS;
    }

    if (meta_unittests_enabled())
    {
        SWSS_LOG_NOTICE("unittests enabled, SET could be performed on %s, not recalculating", meta->attridname);

        return SAI_STATUS_SUCCESS;
    }

    SWSS_LOG_WARN("need to recalculate RO: %s", meta->attridname);

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t vs_create_port_BCM56850(
        _In_ sai_object_id_t port_id,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    // this method is post create action on generic create object

    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_ADMIN_STATE;
    attr.value.booldata = false;     /* default admin state is down as defined in SAI */

    CHECK_STATUS(vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    // attributes are not required since they will be set outside this function

    CHECK_STATUS(create_ingress_priority_groups_per_port(switch_id, port_id));

    CHECK_STATUS(create_qos_queues_per_port(switch_id, port_id));

    CHECK_STATUS(create_scheduler_groups_per_port(switch_id, port_id));

    // TODO should bridge ports should also be created when new port is created?
    // this needs to be checked on real ASIC and updated here

    return SAI_STATUS_SUCCESS;
}
