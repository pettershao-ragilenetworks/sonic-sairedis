#include "SwitchState.h"
#include "RealObjectIdManager.h"
#include "LinkMsg.h"

#include "swss/logger.h"
#include "swss/netdispatcher.h"
#include "swss/netlink.h"
#include "swss/select.h"

#include "meta/sai_serialize.h"

using namespace saivs;

extern std::shared_ptr<RealObjectIdManager>       g_realObjectIdManager;
extern bool g_vs_hostif_use_tap_device; // TODO to be removed

SwitchState::SwitchState(
        _In_ sai_object_id_t switch_id):
    m_switch_id(switch_id)
{
    SWSS_LOG_ENTER();

    if (g_realObjectIdManager->saiObjectTypeQuery(switch_id) != SAI_OBJECT_TYPE_SWITCH)
    {
        SWSS_LOG_THROW("object %s is not SWITCH, its %s",
                sai_serialize_object_id(switch_id).c_str(),
                sai_serialize_object_type(g_realObjectIdManager->saiObjectTypeQuery(switch_id)).c_str());
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

    createNetlinkMessageListener();
}

SwitchState::~SwitchState()
{
    SWSS_LOG_ENTER();

    removeNetlinkMessageListener();
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

void SwitchState::linkMessageThreadFunction()
{
    SWSS_LOG_ENTER();

    LinkMsg linkMsg(m_switch_id);

    swss::NetDispatcher::getInstance().registerMessageHandler(RTM_NEWLINK, &linkMsg);
    swss::NetDispatcher::getInstance().registerMessageHandler(RTM_DELLINK, &linkMsg);

    SWSS_LOG_NOTICE("netlink msg listener started for switch %s",
            sai_serialize_object_id(m_switch_id).c_str());

    while (m_run_link_thread)
    {
        try
        {
            swss::NetLink netlink;

            swss::Select s;

            netlink.registerGroup(RTNLGRP_LINK);
            netlink.dumpRequest(RTM_GETLINK);

            s.addSelectable(&netlink);
            s.addSelectable(&m_link_thread_event);

            while (m_run_link_thread)
            {
                swss::Selectable *sel = NULL;

                int result = s.select(&sel);

                SWSS_LOG_INFO("select ended: %d", result);
            }
        }
        catch (const std::exception& e)
        {
            SWSS_LOG_ERROR("exception: %s", e.what());
            break;
        }
    }

    SWSS_LOG_NOTICE("ending ling message thread for switch: %s",
            sai_serialize_object_id(m_switch_id).c_str());
}

void SwitchState::createNetlinkMessageListener()
{
    SWSS_LOG_ENTER();

    if (g_vs_hostif_use_tap_device == false)
    {
        return;
    }

    m_run_link_thread = false;

    m_link_thread = std::make_shared<std::thread>(&SwitchState::linkMessageThreadFunction, this);
}

void SwitchState::removeNetlinkMessageListener()
{
    SWSS_LOG_ENTER();

    if (g_vs_hostif_use_tap_device == false)
    {
        return;
    }

    m_run_link_thread = false;

    m_link_thread_event.notify();

    m_link_thread->join();
}

