#include "sai_vs.h"
#include <net/if.h>
#include <algorithm>

#include "SwitchStateBase.h"
#include "SwitchBCM56850.h"

using namespace saivs;

// TODO extra work may be needed on GET api if N on list will be > then actual

/*
 * We can use local variable here for initialization (init should be in class
 * constructor anyway, we can move it there later) because each switch init is
 * done under global lock.
 */

static std::shared_ptr<SwitchStateBase> ss;

static sai_status_t set_maximum_number_of_childs_per_scheduler_group()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create switch src mac address");

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_CHILDS_PER_SCHEDULER_GROUP;
    attr.value.u32 = 16;

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
    CHECK_STATUS(ss->create_default_stp_instance());
    CHECK_STATUS(ss->create_default_1q_bridge());
    CHECK_STATUS(ss->create_default_trap_group());
    CHECK_STATUS(ss->create_ports());
    CHECK_STATUS(ss->set_port_list());
    CHECK_STATUS(ss->create_bridge_ports());
    CHECK_STATUS(ss->create_vlan_members());
    CHECK_STATUS(ss->set_acl_entry_min_prio());
    CHECK_STATUS(ss->set_acl_capabilities());
    CHECK_STATUS(ss->create_ingress_priority_groups());
    CHECK_STATUS(ss->create_qos_queues());
    CHECK_STATUS(set_maximum_number_of_childs_per_scheduler_group());
    CHECK_STATUS(set_number_of_ecmp_groups());
    CHECK_STATUS(ss->set_switch_default_attributes());
    CHECK_STATUS(ss->create_scheduler_groups());

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

        // TODO cast right switch or different data pass
        ss = std::dynamic_pointer_cast<SwitchStateBase>(g_switch_state_map[switch_id]);

        warm_boot_initialize_objects();

        SWSS_LOG_NOTICE("initialized switch %s in WARM boot mode", sai_serialize_object_id(switch_id).c_str());

        return;
    }

    if (g_switch_state_map.find(switch_id) != g_switch_state_map.end())
    {
        SWSS_LOG_THROW("switch already exists 0x%lx", switch_id);
    }

    g_switch_state_map[switch_id] = std::make_shared<SwitchBCM56850>(switch_id);

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

    CHECK_STATUS(ss->create_ingress_priority_groups_per_port(switch_id, port_id));
    CHECK_STATUS(ss->create_qos_queues_per_port(switch_id, port_id));
    CHECK_STATUS(ss->create_scheduler_groups_per_port(switch_id, port_id));

    // TODO should bridge ports should also be created when new port is created?
    // this needs to be checked on real ASIC and updated here

    return SAI_STATUS_SUCCESS;
}
