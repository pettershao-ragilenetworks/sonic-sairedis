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

    SWSS_LOG_ERROR();

    // NOTE: process metadata must be executed under sairedis API mutex since
    // it will access meta database and notification comes from different
    // thread, and this method is executed from notifications thread

    notification->processMetadata();

    auto objectId = notification->getAnyObjectId();

    auto switchId = g_virtualObjectIdManager->saiSwitchIdQuery(objectId);

    return getSwitchNotifications(switchId);
}

void handle_notification(
        _In_ const std::string &name,
        _In_ const std::string &serializedNotification,
        _In_ const std::vector<swss::FieldValueTuple> &values)
{
    SWSS_LOG_ENTER();

    // TODO to pass switch_id for every notification we could add it to values
    // at syncd side
    //
    // Each global context (syncd) will have it's own notification thread
    // handler, so we will know at which context notification arrived, but we
    // also need to know at which switch id generated this notification. For
    // that we will assign separate notification handlers in syncd itself, and
    // each of those notifications will know to which switch id it belongs.
    // Then later we could also check whether oids in notification actually
    // belongs to given switch id.  This way we could find vendor bugs like
    // sending notifications from one switch to another switch handler.
    //
    // But before that we will extract switch id from notification itself.

    // TODO record should also be under api mutex, all other apis are

    g_recorder->recordNotification(name, serializedNotification, values);

    auto notification = NotificationFactory::deserialize(name, serializedNotification);

    if (notification)
    {
        // process is done under api mutex

        auto sn = processNotification(notification);

        // execute callback from thread context

        notification->executeCallback(sn);
    }
}
