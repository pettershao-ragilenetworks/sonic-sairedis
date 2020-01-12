#include "LinkMsg.h"

#include "swss/logger.h"
#include "meta/sai_serialize.h"
#include "SwitchState.h"

#include <netlink/route/link.h>
#include <netlink/route/addr.h>
#include <linux/if.h>

#include "sai_vs.h" // TODO to be removed

using namespace saivs;

LinkMsg::LinkMsg(
        _In_ sai_object_id_t switch_id)
    : m_switch_id(switch_id)
{
    SWSS_LOG_ENTER();

    // empty
}

void LinkMsg::onMsg(
        _In_ int nlmsg_type, 
        _In_ struct nl_object *obj)
{
    SWSS_LOG_ENTER();

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

    // TODO get index when we will have multiple switches
    auto map = g_laneMapContainer->getLaneMap(0);

    if (!map)
    {
        SWSS_LOG_ERROR("lane map for index %u don't exists", 0);
        return;
    }

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

    std::shared_ptr<SwitchState> sw = vs_get_switch_state(m_switch_id);

    if (sw == nullptr)
    {
        SWSS_LOG_ERROR("failed to get switch state for switch id %s",
                sai_serialize_object_id(m_switch_id).c_str());
        return;
    }

    auto port_id = sw->getPortIdFromIfName(ifname); // TODO needs to be protected under lock

    if (port_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("failed to find port id for interface %s", ifname.c_str());
        return;
    }

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY;

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

    update_port_oper_status(port_id, data.port_state);

    SWSS_LOG_DEBUG("executing callback SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY for port %s: %s",
            sai_serialize_object_id(data.port_id).c_str(),
            sai_serialize_port_oper_status(data.port_state).c_str());

    callback(1, &data);
}
