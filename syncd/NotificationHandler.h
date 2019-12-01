#include "syncd.h"

class NotificationHandler
{
    public:

        NotificationHandler();

        virtual ~NotificationHandler() = default;

    public:

        void setSwitchNotifications(
                _In_ const sai_switch_notifications_t& switchNotifications);

        const sai_switch_notifications_t& getSwitchNotifications() const;

        void updateNotificationsPointers(
                _In_ uint32_t attr_count,
                _In_ sai_attribute_t *attr_list) const;

    public: // members reflecting SAI callbacks

        void onFdbEvent(
                _In_ uint32_t count,
                _In_ const sai_fdb_event_notification_data_t *data);

        void onPortStateChange(
                _In_ uint32_t count,
                _In_ const sai_port_oper_status_notification_t *data);

        void onQueuePfcDeadlock(
                _In_ uint32_t count,
                _In_ const sai_queue_deadlock_notification_data_t *data);

        void onSwitchShutdownRequest(
                _In_ sai_object_id_t switch_id);

        void onSwitchStateChange(
                _In_ sai_object_id_t switch_id,
                _In_ sai_switch_oper_status_t switch_oper_status);

    private:

        void enqueueNotification(
                _In_ const std::string& op,
                _In_ const std::string& data,
                _In_ const std::vector<swss::FieldValueTuple> &entry);

        void enqueueNotification(
                _In_ const std::string& op,
                _In_ const std::string& data);

    private:

        sai_switch_notifications_t m_switchNotifications;

        std::make_shared<NotificationQueue> m_notificationQueue;
};

