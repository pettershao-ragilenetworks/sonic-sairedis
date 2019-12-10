#include "NotificationSwitchShutdownRequest.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"
#include "meta/sai_meta.h"

using namespace sairedis;

NotificationSwitchShutdownRequest::NotificationSwitchShutdownRequest(
        _In_ const std::string& serializedNotification):
    Notification(
            SAI_SWITCH_NOTIFICATION_TYPE_SWITCH_SHUTDOWN_REQUEST,
            serializedNotification)
{
    SWSS_LOG_ENTER();

    sai_deserialize_switch_shutdown_request(serializedNotification, m_switchId);
}

sai_object_id_t NotificationSwitchShutdownRequest::getSwitchId() const
{
    SWSS_LOG_ENTER();

    return m_switchId;
}

sai_object_id_t NotificationSwitchShutdownRequest::getAnyObjectId() const
{
    SWSS_LOG_ENTER();

    return m_switchId;
}

void NotificationSwitchShutdownRequest::processMetadata() const
{
    SWSS_LOG_ENTER();

    // TODO add parameter with metadata object, currently we are calling global
    // functions

    meta_sai_on_switch_shutdown_request(m_switchId);
}

void NotificationSwitchShutdownRequest::executeCallback(
        _In_ const sai_switch_notifications_t& switchNotifications) const
{
    SWSS_LOG_ENTER();

    if (switchNotifications.on_switch_shutdown_request)
    {
        switchNotifications.on_switch_shutdown_request(m_switchId);
    }
}

