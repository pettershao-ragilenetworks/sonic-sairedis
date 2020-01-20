#include "NetMsgRegistrar.h"
#include "SwitchState.h"
#include "RealObjectIdManager.h"
#include "SwitchStateBase.h"
#include "RealObjectIdManager.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"

#include <netlink/route/link.h>
#include <netlink/route/addr.h>
#include <linux/if.h>

#include "sai_vs.h" // TODO to be removed

using namespace saivs;

// TODO MUTEX must be used when adding and removing interface index by system

#define VS_COUNTERS_COUNT_MSB (0x80000000)

#define MUTEX std::lock_guard<std::mutex> _lock(m_mutex);

SwitchState::SwitchState(
        _In_ sai_object_id_t switch_id,
        _In_ std::shared_ptr<SwitchConfig> config):
    m_switch_id(switch_id),
    m_linkCallbackIndex(-1),
    m_switchConfig(config)
{
    SWSS_LOG_ENTER();

    // TODO we would not like to use real manager static method here
    if (RealObjectIdManager::objectTypeQuery(switch_id) != SAI_OBJECT_TYPE_SWITCH)
    {
        SWSS_LOG_THROW("object %s is not SWITCH, its %s",
                sai_serialize_object_id(switch_id).c_str(),
                sai_serialize_object_type(RealObjectIdManager::objectTypeQuery(switch_id)).c_str());
    }

    for (int i = SAI_OBJECT_TYPE_NULL; i < (int)SAI_OBJECT_TYPE_EXTENSIONS_MAX; ++i)
    {
        /*
         * Populate empty maps for each object to avoid checking if
         * objecttype exists.
         */

        m_objectHash[(sai_object_type_t)i] = { };
    }

    /*
     * Create switch by default, it will require special treat on
     * creating.
     */

    m_objectHash[SAI_OBJECT_TYPE_SWITCH][sai_serialize_object_id(switch_id)] = {};

    if (m_switchConfig->m_useTapDevice)
    {
        m_linkCallbackIndex = NetMsgRegistrar::getInstance().registerCallback(
                std::bind(&SwitchState::asyncOnLinkMsg, this, std::placeholders::_1, std::placeholders::_2));
    }
}

SwitchState::~SwitchState()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("begin");

    if (m_switchConfig->m_useTapDevice)
    {
        NetMsgRegistrar::getInstance().unregisterCallback(m_linkCallbackIndex);

        // if unregister ended then no new messages will arrive for this class
        // so there is no need to protect this using mutex
    }

    SWSS_LOG_NOTICE("switch %s",
            sai_serialize_object_id(m_switch_id).c_str());

    SWSS_LOG_NOTICE("end");
}

void SwitchState::setMeta(
        std::weak_ptr<saimeta::Meta> meta)
{
    SWSS_LOG_ENTER();

    m_meta = meta;
}

sai_object_id_t SwitchState::getSwitchId() const
{
    SWSS_LOG_ENTER();

    return m_switch_id;
}

void SwitchState::setIfNameToPortId(
        _In_ const std::string& ifname,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    m_ifname_to_port_id_map[ifname] = port_id;
}


void SwitchState::removeIfNameToPortId(
        _In_ const std::string& ifname)
{
    SWSS_LOG_ENTER();

    m_ifname_to_port_id_map.erase(ifname);
}

sai_object_id_t SwitchState::getPortIdFromIfName(
        _In_ const std::string& ifname) const
{
    SWSS_LOG_ENTER();

    auto it = m_ifname_to_port_id_map.find(ifname);

    if (it == m_ifname_to_port_id_map.end())
    {
        return SAI_NULL_OBJECT_ID;
    }

    return it->second;
}

void SwitchState::setPortIdToTapName(
        _In_ sai_object_id_t port_id,
        _In_ const std::string& tapname)
{
    SWSS_LOG_ENTER();

    m_port_id_to_tapname[port_id] = tapname;
}

void SwitchState::removePortIdToTapName(
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    m_port_id_to_tapname.erase(port_id);
}

bool SwitchState::getTapNameFromPortId(
        _In_ const sai_object_id_t port_id,
        _Out_ std::string& if_name)
{
    SWSS_LOG_ENTER();

    if (m_port_id_to_tapname.find(port_id) != m_port_id_to_tapname.end())
    {
        if_name = m_port_id_to_tapname[port_id];

        return true;
    }

    return false;
}

void SwitchState::asyncOnLinkMsg(
        _In_ int nlmsg_type,
        _In_ struct nl_object *obj)
{
    SWSS_LOG_ENTER();

    MUTEX;

    // TODO insert to event queue

    // TODO this content must be executed under global mutex

    if (nlmsg_type == RTM_DELLINK)
    {
        struct rtnl_link *link = (struct rtnl_link *)obj;
        const char* name = rtnl_link_get_name(link);

        SWSS_LOG_NOTICE("received RTM_DELLINK for %s", name);
        return;
    }

    if (nlmsg_type != RTM_NEWLINK)
    {
        SWSS_LOG_WARN("unsupported nlmsg_type: %d", nlmsg_type);
        return;
    }

    // new link

    struct rtnl_link *link = (struct rtnl_link *)obj;

    int             if_index = rtnl_link_get_ifindex(link);
    unsigned int    if_flags = rtnl_link_get_flags(link); // IFF_LOWER_UP and IFF_RUNNING
    const char*     if_name  = rtnl_link_get_name(link);

    // TODO on warm boot we must recreate lane map
    // or lane map should only be used on create
    // if during switch shutdown lane map changed (port added/removed)
    // then loaded lane map will point to wrong mapping

    // TODO get index when we will have multiple switches
    auto map = m_switchConfig->m_laneMap;

    if (!map)
    {
        SWSS_LOG_ERROR("lane map for index %u don't exists", 0);
        return;
    }

    // TODO we must check if_index also if index is registered under this switch

    if (strncmp(if_name, SAI_VS_VETH_PREFIX, sizeof(SAI_VS_VETH_PREFIX) - 1) != 0 &&
            !map->hasInterface(if_name))
    {
        SWSS_LOG_INFO("skipping newlink for %s", if_name);
        return;
    }

    SWSS_LOG_NOTICE("newlink: ifindex: %d, ifflags: 0x%x, ifname: %s",
            if_index,
            if_flags,
            if_name);

    std::string ifname(if_name);

    auto port_id = getPortIdFromIfName(ifname); // TODO needs to be protected under lock

    if (port_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("failed to find port id for interface %s", ifname.c_str());
        return;
    }

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY;

    // TODO to be removed
    if (vs_switch_api.get_switch_attribute(m_switch_id, 1, &attr) != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to get SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY for switch %s",
                sai_serialize_object_id(m_switch_id).c_str());
        return;
    }

    sai_port_state_change_notification_fn callback =
        (sai_port_state_change_notification_fn)attr.value.ptr;

    if (callback == NULL)
    {
        return;
    }

    sai_port_oper_status_notification_t data;

    data.port_id = port_id;
    data.port_state = (if_flags & IFF_LOWER_UP) ? SAI_PORT_OPER_STATUS_UP : SAI_PORT_OPER_STATUS_DOWN;

    attr.id = SAI_PORT_ATTR_OPER_STATUS;

    // TODO to be removed
    if (vs_port_api.get_port_attribute(port_id, 1, &attr) != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to get port attribute SAI_PORT_ATTR_OPER_STATUS");
    }
    else
    {
        if ((sai_port_oper_status_t)attr.value.s32 == data.port_state)
        {
            SWSS_LOG_DEBUG("port oper status didn't changed, will not send notification");
            return;
        }
    }

    // TODO remove cast
    auto*base = dynamic_cast<SwitchStateBase*>(this);

    base->update_port_oper_status(port_id, data.port_state);

    SWSS_LOG_DEBUG("executing callback SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY for port %s: %s",
            sai_serialize_object_id(data.port_id).c_str(),
            sai_serialize_port_oper_status(data.port_state).c_str());

    // TODO we should also call Meta for this notification

    callback(1, &data);
}

sai_status_t SwitchState::getStatsExt(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t* counter_ids,
        _In_ sai_stats_mode_t mode,
        _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    bool perform_set = false;

    auto info = sai_metadata_get_object_type_info(object_type);

    bool enabled = false;

    auto meta = m_meta.lock();

    if (meta)
    {
        enabled = meta->meta_unittests_enabled();
    }
    else
    {
        SWSS_LOG_WARN("meta poiner expired");
    }

    if (enabled && (number_of_counters & VS_COUNTERS_COUNT_MSB ))
    {
        number_of_counters &= ~VS_COUNTERS_COUNT_MSB;

        SWSS_LOG_NOTICE("unittests are enabled and counters count MSB is set to 1, performing SET on %s counters (%s)",
                sai_serialize_object_id(object_id).c_str(),
                info->statenum->name);

        perform_set = true;
    }

    auto str_object_id = sai_serialize_object_id(object_id);

    auto mapit = m_countersMap.find(str_object_id);

    if (mapit == m_countersMap.end())
        m_countersMap[str_object_id] = { };

    auto& localcounters = m_countersMap[str_object_id];

    for (uint32_t i = 0; i < number_of_counters; ++i)
    {
        int32_t id = counter_ids[i];

        if (perform_set)
        {
            localcounters[ id ] = counters[i];
        }
        else
        {
            auto it = localcounters.find(id);

            if (it == localcounters.end())
            {
                // if counter is not found on list, just return 0
                counters[i] = 0;
            }
            else
            {
                counters[i] = it->second;
            }

            if (mode == SAI_STATS_MODE_READ_AND_CLEAR)
            {
                localcounters[ id ] = 0;
            }
        }
    }

    return SAI_STATUS_SUCCESS;
}

