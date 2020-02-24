#include "NotificationSwitchStateChange.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"
#include "meta/sai_meta.h"

using namespace sairedis;

NotificationSwitchStateChange::NotificationSwitchStateChange(
        _In_ const std::string& serializedNotification):
    Notification(
            SAI_SWITCH_NOTIFICATION_TYPE_SWITCH_STATE_CHANGE,
            serializedNotification)
{
    SWSS_LOG_ENTER();

    sai_deserialize_switch_oper_status(
            serializedNotification,
            m_switchId,
            m_switchOperStatus);
}

sai_object_id_t NotificationSwitchStateChange::getSwitchId() const
{
    SWSS_LOG_ENTER();

    return m_switchId;
}

sai_object_id_t NotificationSwitchStateChange::getAnyObjectId() const
{
    SWSS_LOG_ENTER();

    return m_switchId;
}

void NotificationSwitchStateChange::processMetadata() const
{
    SWSS_LOG_ENTER();

    // TODO add parameter with metadata object, currently we are calling global
    // functions

    meta_sai_on_switch_state_change(m_switchId, m_switchOperStatus);
}

void NotificationSwitchStateChange::executeCallback(
        _In_ const sai_switch_notifications_t& switchNotifications) const
{
    SWSS_LOG_ENTER();

    if (switchNotifications.on_switch_state_change)
    {
        switchNotifications.on_switch_state_change(m_switchId, m_switchOperStatus);
    }
}
