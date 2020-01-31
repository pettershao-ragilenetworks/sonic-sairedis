#include "NotificationHandlerWrapper.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"

using namespace syncd;

std::shared_ptr<NotificationHandler> NotificationHandlerWrapper::m_handler = nullptr;

void NotificationHandlerWrapper::setNotificationHandler(
        _In_ std::shared_ptr<NotificationHandler> handler)
{
    SWSS_LOG_ENTER();

    m_handler = handler;

    sai_switch_notifications_t sn = { };

    // TODO add other callbacks when needed

    sn.on_bfd_session_state_change = NULL;
    sn.on_fdb_event = NotificationHandlerWrapper::onFdbEvent;
    sn.on_packet_event = NULL;
    sn.on_port_state_change = NotificationHandlerWrapper::onPortStateChange;
    sn.on_queue_pfc_deadlock = NotificationHandlerWrapper::onQueuePfcDeadlock;
    sn.on_switch_shutdown_request = NotificationHandlerWrapper::onSwitchShutdownRequest;
    sn.on_switch_state_change= NotificationHandlerWrapper::onSwitchStateChange;
    sn.on_tam_event = NULL;

    handler->setSwitchNotifications(sn);
}

void NotificationHandlerWrapper::onFdbEvent(
        _In_ uint32_t count,
        _In_ const sai_fdb_event_notification_data_t *data)
{
    SWSS_LOG_ENTER();

    m_handler->onFdbEvent(count, data);
}

void NotificationHandlerWrapper::onPortStateChange(
        _In_ uint32_t count,
        _In_ const sai_port_oper_status_notification_t *data)
{
    SWSS_LOG_ENTER();

    m_handler->onPortStateChange(count, data);
}

void NotificationHandlerWrapper::onQueuePfcDeadlock(
        _In_ uint32_t count,
        _In_ const sai_queue_deadlock_notification_data_t *data)
{
    SWSS_LOG_ENTER();

    m_handler->onQueuePfcDeadlock(count, data);
}

void NotificationHandlerWrapper::onSwitchShutdownRequest(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    m_handler->onSwitchShutdownRequest(switch_id);
}

void NotificationHandlerWrapper::onSwitchStateChange(
        _In_ sai_object_id_t switch_id,
        _In_ sai_switch_oper_status_t switch_oper_status)
{
    SWSS_LOG_ENTER();

    m_handler->onSwitchStateChange(switch_id, switch_oper_status);
}

