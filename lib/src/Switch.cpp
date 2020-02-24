#include "Switch.h"

#include "swss/logger.h"

#include <cstring>

using namespace sairedis;

Switch::Switch(
        _In_ sai_object_id_t switchId):
    m_switchId(switchId)
{
    SWSS_LOG_ENTER();

    if (switchId == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_THROW("switch id can't be NULL");
    }

    clearNotificationsPointers();
}

Switch::Switch(
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList):
    Switch(switchId)
{
    SWSS_LOG_ENTER();

    updateNotifications(attrCount, attrList);

    // SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO is create only attribute

    m_hardwareInfo = getHardwareInfo(attrCount, attrList);

    SWSS_LOG_NOTICE("created switch with hwinfo = '%s'", m_hardwareInfo.c_str());
}

void Switch::clearNotificationsPointers()
{
    SWSS_LOG_ENTER();

    memset(&m_switchNotifications, 0, sizeof(m_switchNotifications));
}

sai_object_id_t Switch::getSwitchId() const
{
    SWSS_LOG_ENTER();

    return m_switchId;
}

void Switch::updateNotifications(
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList)
{
    SWSS_LOG_ENTER();

    /*
     * This function should only be called on CREATE/SET
     * api when object is SWITCH.
     */

    for (uint32_t index = 0; index < attrCount; ++index)
    {
        auto &attr = attrList[index];

        auto meta = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_SWITCH, attr.id);

        if (meta == NULL)
            SWSS_LOG_THROW("failed to find metadata for switch attr %d", attr.id);

        if (meta->attrvaluetype != SAI_ATTR_VALUE_TYPE_POINTER)
            continue;

        switch (attr.id)
        {
            case SAI_SWITCH_ATTR_SWITCH_STATE_CHANGE_NOTIFY:
                m_switchNotifications.on_switch_state_change =
                    (sai_switch_state_change_notification_fn)attr.value.ptr;
                break;

            case SAI_SWITCH_ATTR_SHUTDOWN_REQUEST_NOTIFY:
                m_switchNotifications.on_switch_shutdown_request =
                    (sai_switch_shutdown_request_notification_fn)attr.value.ptr;
                break;

            case SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY:
                m_switchNotifications.on_fdb_event =
                    (sai_fdb_event_notification_fn)attr.value.ptr;
                break;

            case SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY:
                m_switchNotifications.on_port_state_change =
                    (sai_port_state_change_notification_fn)attr.value.ptr;
                break;

            case SAI_SWITCH_ATTR_PACKET_EVENT_NOTIFY:
                m_switchNotifications.on_packet_event =
                    (sai_packet_event_notification_fn)attr.value.ptr;
                break;

            case SAI_SWITCH_ATTR_QUEUE_PFC_DEADLOCK_NOTIFY:
                m_switchNotifications.on_queue_pfc_deadlock =
                    (sai_queue_pfc_deadlock_notification_fn)attr.value.ptr;
                break;

            default:
                SWSS_LOG_ERROR("pointer for %s is not handled, FIXME!", meta->attridname);
                break;
        }
    }
}

const sai_switch_notifications_t& Switch::getSwitchNotifications() const
{
    SWSS_LOG_ENTER();

    return m_switchNotifications;
}

const std::string& Switch::getHardwareInfo() const
{
    SWSS_LOG_ENTER();

    return m_hardwareInfo;
}

std::string Switch::getHardwareInfo(
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList)
{
    SWSS_LOG_ENTER();

    auto *attr = sai_metadata_get_attr_by_id(
            SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO,
            attrCount,
            attrList);

    if (attr == NULL)
        return "";

    auto& s8list = attr->value.s8list;

    if (s8list.count == 0)
        return "";

    if (s8list.list == NULL)
    {
        SWSS_LOG_WARN("SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO s8list.list is NULL! but count is %u", s8list.count);
        return "";
    }

    uint32_t count = s8list.count;

    if (count > SAI_MAX_HARDWARE_ID_LEN)
    {
        SWSS_LOG_WARN("SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO s8list.count (%u) > SAI_MAX_HARDWARE_ID_LEN (%d), LIMITING !!",
                count,
                SAI_MAX_HARDWARE_ID_LEN);

        count = SAI_MAX_HARDWARE_ID_LEN;
    }

    // check actual length, since buffer may contain nulls
    auto actualLength = strnlen((const char*)s8list.list, count);

    return std::string((const char*)s8list.list, actualLength);
}

