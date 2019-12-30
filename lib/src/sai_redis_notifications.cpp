#include "sai_redis.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

#include "NotificationFactory.h"

using namespace sairedis;

/**
 * @brief Get switch notifications structure.
 *
 * This function is executed in notifications thread, and it may happen that
 * during switch remove some notification arrived for that switch, so we need
 * to prevent race condition that switch will be removed before notification
 * will be processed.
 *
 * @return Copy of requested switch notifications struct or empty struct is
 * switch is not present in container.
 */
static sai_switch_notifications_t getSwitchNotifications(
        _In_ sai_object_id_t switchId)
{
    SWSS_LOG_ENTER();

    auto sw = g_switchContainer->getSwitch(switchId);

    if (sw)
    {
        return sw->getSwitchNotifications(); // explicit copy
    }

    SWSS_LOG_WARN("switch %s not present in container, returning empty switch notifications",
            sai_serialize_object_id(switchId).c_str());

    return sai_switch_notifications_t { };
}

// We are assuming that notifications that has "count" member and don't have
// explicit switch_id defined in struct (like fdb event), they will always come
// from the same switch instance (same switch id). This is because we can define
// different notifications pointers per switch instance. Similar notification
// on_queue_pfc_deadlock_notification which only have queue_id

static sai_switch_notifications_t processNotification(
        _In_ std::shared_ptr<Notification> notification)
{
    MUTEX();

    SWSS_LOG_ENTER();

    // NOTE: process metadata must be executed under sairedis API mutex since
    // it will access meta database and notification comes from different
    // thread, and this method is executed from notifications thread

    notification->processMetadata(g_meta);

    auto objectId = notification->getAnyObjectId();

    auto switchId = g_virtualObjectIdManager->saiSwitchIdQuery(objectId);

    return getSwitchNotifications(switchId);
}

void handle_notification(
        _In_ std::shared_ptr<Notification> notification)
{
    SWSS_LOG_ENTER();

    if (notification)
    {
        // process is done under api mutex

        auto sn = processNotification(notification);

        // execute callback from thread context

        notification->executeCallback(sn);
    }
}
