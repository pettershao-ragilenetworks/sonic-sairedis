#include "sai_vs.h"
#include <net/if.h>

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

    ss = std::dynamic_pointer_cast<SwitchStateBase>(g_switch_state_map[switch_id]);

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
                return ss->refresh_port_list(meta, switch_id);

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
                return ss->refresh_qos_queues(meta, object_id, switch_id);

            case SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS:
            case SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST:
                return ss->refresh_ingress_priority_group(meta, object_id, switch_id);

            case SAI_PORT_ATTR_QOS_NUMBER_OF_SCHEDULER_GROUPS:
            case SAI_PORT_ATTR_QOS_SCHEDULER_GROUP_LIST:
                return ss->refresh_scheduler_groups(meta, object_id, switch_id);

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
                return ss->refresh_scheduler_groups(meta, object_id, switch_id);
        }
    }

    if (meta->objecttype == SAI_OBJECT_TYPE_BRIDGE && meta->attrid == SAI_BRIDGE_ATTR_PORT_LIST)
    {
        return ss->refresh_bridge_port_list(meta, object_id, switch_id);
    }

    if (meta->objecttype == SAI_OBJECT_TYPE_VLAN && meta->attrid == SAI_VLAN_ATTR_MEMBER_LIST)
    {
        return ss->refresh_vlan_member_list(meta, object_id, switch_id);
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

