#include "NotificationHandler.h"

NotificationHandler::NotificationHandler()
{
    SWSS_LOG_ENTER();

    memset(&m_switchNotifications, 0, sizeof(m_switchNotifications));

    m_notificationQueue = std::make_shared<NotificationQueue>();
}

void NotificationHandler::setSwitchNotifications(
        _In_ const sai_switch_notifications_t& switchNotifications)
{
    SWSS_LOG_ENTER();

    m_switchNotifications = switchNotifications;
}

const sai_switch_notifications_t& NotificationHandler::getSwitchNotifications() const
{
    SWSS_LOG_ENTER();

    return m_switchNotifications;
}

void NotificationHandler::updateNotificationsPointers(
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list) const
{
    SWSS_LOG_ENTER();

    /*
     * This function should only be called on CREATE/SET api when object is
     * SWITCH.
     *
     * Notifications pointers needs to be corrected since those we receive from
     * sairedis are in sairedis memory space and here we are using those ones
     * we declared in syncd memory space.
     *
     * Also notice that we are using the same pointers for ALL switches.
     */

    for (uint32_t index = 0; index < attr_count; ++index)
    {
        sai_attribute_t &attr = attr_list[index];

        auto meta = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_SWITCH, attr.id);

        if (meta->attrvaluetype != SAI_ATTR_VALUE_TYPE_POINTER)
        {
            continue;
        }

        /*
         * Does not matter if pointer is valid or not, we just want the
         * previous value.
         */

        sai_pointer_t prev = attr.value.ptr;

        if (prev == NULL)
        {
            /*
             * If pointer is NULL, then fine, let it be.
             */

            continue;
        }

        switch (attr.id)
        {
            case SAI_SWITCH_ATTR_SWITCH_STATE_CHANGE_NOTIFY:
                attr.value.ptr = (void*)m_switchNotifications.on_switch_state_change;
                break;

            case SAI_SWITCH_ATTR_SHUTDOWN_REQUEST_NOTIFY:
                attr.value.ptr = (void*)m_switchNotifications.on_switch_shutdown_request;
                break;

            case SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY:
                attr.value.ptr = (void*)m_switchNotifications.on_fdb_event;
                break;

            case SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY:
                attr.value.ptr = (void*)m_switchNotifications.on_port_state_change;
                break;

            case SAI_SWITCH_ATTR_PACKET_EVENT_NOTIFY:
                attr.value.ptr = (void*)m_switchNotifications.on_packet_event;
                break;

            case SAI_SWITCH_ATTR_QUEUE_PFC_DEADLOCK_NOTIFY:
                attr.value.ptr = (void*)m_switchNotifications.on_queue_pfc_deadlock;
                break;

            default:

                SWSS_LOG_ERROR("pointer for %s is not handled, FIXME!", meta->attridname);
                continue;
        }

        /*
         * Here we translated pointer, just log it.
         */

        SWSS_LOG_INFO("%s: 0x%" PRIx64 " (orch) => 0x%" PRIx64 " (syncd)", meta->attridname, (uint64_t)prev, (uint64_t)attr.value.ptr);
    }
}

void NotificationHandler::onFdbEvent(
        _In_ uint32_t count,
        _In_ const sai_fdb_event_notification_data_t *data)
{
    SWSS_LOG_ENTER();

    std::string s = sai_serialize_fdb_event_ntf(count, data);

    enqueueNotification("fdb_event", s);
}

void NotificationHandler::onPortStateChange(
        _In_ uint32_t count,
        _In_ const sai_port_oper_status_notification_t *data)
{
    SWSS_LOG_ENTER();

    std::string s = sai_serialize_port_oper_status_ntf(count, data);

    enqueueNotification("port_state_change", s);
}

void NotificationHandler::onQueuePfcDeadlock(
        _In_ uint32_t count,
        _In_ const sai_queue_deadlock_notification_data_t *data)
{
    SWSS_LOG_ENTER();

    std::string s = sai_serialize_queue_deadlock_ntf(count, data);

    enqueueNotification("queue_deadlock", s);
}

void NotificationHandler::onSwitchShutdownRequest(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    std::string s = sai_serialize_switch_shutdown_request(switch_id);

    enqueueNotification("switch_shutdown_request", "");
}

void NotificationHandler::onSwitchStateChange(
        _In_ sai_object_id_t switch_id,
        _In_ sai_switch_oper_status_t switch_oper_status)
{
    SWSS_LOG_ENTER();

    std::string s = sai_serialize_switch_oper_status(switch_id, switch_oper_status);

    enqueueNotification("switch_state_change", s);
}

void NotificationHandler::enqueueNotification(
        _In_ const std::string& op,
        _In_ const std::string& data,
        _In_ const std::vector<swss::FieldValueTuple> &entry)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("%s %s", op.c_str(), data.c_str());

    swss::KeyOpFieldsValuesTuple item(op, data, entry);

    if (m_notificationQueue->enqueue(item))
    {
        cv.notify_all();
    }
}

void NotificationHandler::enqueueNotification(
        _In_ const std::string& op,
        _In_ const std::string& data)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> entry;

    enqueueNotification(op, data, entry);
}

