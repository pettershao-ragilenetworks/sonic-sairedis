#include "sai_vs.h"
#include "sai_vs_internal.h"

#include "swss/selectableevent.h"
#include "swss/netmsg.h"
#include "swss/netlink.h"
#include "swss/select.h"
#include "swss/netdispatcher.h"

#include "LinkMsg.h"

#include <thread>

using namespace saivs;

/**
 * @brief Get SwitchState by switch id.
 *
 * Function will get shared object for switch state.  This function is thread
 * safe and it's only intended to use inside threads.
 *
 * @param switch_id Switch ID
 *
 * @return SwitchState object or null ptr if not found.
 */
std::shared_ptr<SwitchState> vs_get_switch_state(
        _In_ sai_object_id_t switch_id)
{
    MUTEX();

    SWSS_LOG_ENTER();

    auto it = g_switch_state_map.find(switch_id);

    if (it == g_switch_state_map.end())
    {
        return nullptr;
    }

    return it->second;
}

void update_port_oper_status(
        _In_ sai_object_id_t port_id,
        _In_ sai_port_oper_status_t port_oper_status)
{
    MUTEX();

    SWSS_LOG_ENTER();

    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_OPER_STATUS;
    attr.value.s32 = port_oper_status;

    sai_status_t status = vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to update port status %s: %s",
                sai_serialize_object_id(port_id).c_str(),
                sai_serialize_port_oper_status(port_oper_status).c_str());
    }
}

void linkmsg_thread(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    std::shared_ptr<SwitchState> sw = vs_get_switch_state(switch_id);

    if (sw == nullptr)
    {
        SWSS_LOG_ERROR("failed to get switch state for switch id %s",
                sai_serialize_object_id(switch_id).c_str());
        return;
    }

    LinkMsg linkMsg(switch_id);

    swss::NetDispatcher::getInstance().registerMessageHandler(RTM_NEWLINK, &linkMsg);
    swss::NetDispatcher::getInstance().registerMessageHandler(RTM_DELLINK, &linkMsg);

    SWSS_LOG_NOTICE("netlink msg listener started for switch %s",
            sai_serialize_object_id(switch_id).c_str());

    while (sw->getRunLinkThread())
    {
        try
        {
            swss::NetLink netlink;

            swss::Select s;

            netlink.registerGroup(RTNLGRP_LINK);
            netlink.dumpRequest(RTM_GETLINK);

            s.addSelectable(&netlink);
            s.addSelectable(sw->getLinkThreadEvent());

            while (sw->getRunLinkThread())
            {
                swss::Selectable *temps = NULL;

                int result = s.select(&temps);

                SWSS_LOG_INFO("select ended: %d", result);
            }
        }
        catch (const std::exception& e)
        {
            SWSS_LOG_ERROR("exception: %s", e.what());
            return;
        }
    }

    SWSS_LOG_NOTICE("ending ling message thread");
}

void vs_create_netlink_message_listener(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    if (g_vs_hostif_use_tap_device == false)
    {
        return;
    }

    auto sw = vs_get_switch_state(switch_id);

    if (sw == nullptr)
    {
        SWSS_LOG_ERROR("failed to get switch state for switch id %s",
                sai_serialize_object_id(switch_id).c_str());
        return;
    }

    sw->setRunLinkThread(true);

    std::shared_ptr<std::thread> linkThread =
        std::make_shared<std::thread>(linkmsg_thread, switch_id);

    sw->setLinkThread(linkThread);
}

sai_status_t vs_create_switch(
        _Out_ sai_object_id_t *switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    MUTEX();
    SWSS_LOG_ENTER();
    VS_CHECK_API_INITIALIZED();

    sai_status_t status = meta_sai_create_oid(
            SAI_OBJECT_TYPE_SWITCH,
            switch_id,
            SAI_NULL_OBJECT_ID, // no switch id since we create switch
            attr_count,
            attr_list,
            &vs_generic_create);

    if (status == SAI_STATUS_SUCCESS)
    {
        vs_create_netlink_message_listener(*switch_id);

        auto sw = std::make_shared<Switch>(*switch_id, attr_count, attr_list);

        g_switchContainer->insert(sw);
    }

    return status;
}

sai_status_t vs_remove_switch(
            _In_ sai_object_id_t switch_id)
{
    MUTEX();
    SWSS_LOG_ENTER();
    VS_CHECK_API_INITIALIZED();

    sai_status_t status = meta_sai_remove_oid(
            SAI_OBJECT_TYPE_SWITCH,
            switch_id,
            &vs_generic_remove);

    if (status == SAI_STATUS_SUCCESS)
    {
        // remove switch from container

        g_switchContainer->removeSwitch(switch_id);
    }

    return status;
}

sai_status_t vs_set_switch_attribute(
        _In_ sai_object_id_t switch_id,
        _In_ const sai_attribute_t *attr)
{
    MUTEX();
    SWSS_LOG_ENTER();
    VS_CHECK_API_INITIALIZED();

    sai_status_t status = meta_sai_set_oid(
            SAI_OBJECT_TYPE_SWITCH,
            switch_id,
            attr,
            &vs_generic_set);

    if (status == SAI_STATUS_SUCCESS)
    {
        auto sw = g_switchContainer->getSwitch(switch_id);

        if (!sw)
        {
            SWSS_LOG_THROW("failed to find switch %s in container",
                    sai_serialize_object_id(switch_id).c_str());
        }

        /*
         * When doing SET operation user may want to update notification
         * pointers.
         */
        sw->updateNotifications(1, attr);
    }

    return status;
}

VS_GET(SWITCH,switch);
VS_GENERIC_STATS(SWITCH, switch);

const sai_switch_api_t vs_switch_api = {

    vs_create_switch,
    vs_remove_switch,
    vs_set_switch_attribute,
    vs_get_switch_attribute,
    VS_GENERIC_STATS_API(switch)
};
