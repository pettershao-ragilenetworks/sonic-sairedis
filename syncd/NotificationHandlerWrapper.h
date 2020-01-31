#pragma once

#include "NotificationHandler.h"

namespace syncd
{
    class NotificationHandlerWrapper
    {
        private:

            NotificationHandlerWrapper() = delete;

            virtual ~NotificationHandlerWrapper() = delete;

        private:

            void operator=(NotificationHandlerWrapper const&) = delete;

        public:

            static void setNotificationHandler(
                    _In_ std::shared_ptr<NotificationHandler> handler);

        public: // static members reflecting SAI callbacks

            static void onFdbEvent(
                    _In_ uint32_t count,
                    _In_ const sai_fdb_event_notification_data_t *data);

            static void onPortStateChange(
                    _In_ uint32_t count,
                    _In_ const sai_port_oper_status_notification_t *data);

            static void onQueuePfcDeadlock(
                    _In_ uint32_t count,
                    _In_ const sai_queue_deadlock_notification_data_t *data);

            static void onSwitchShutdownRequest(
                    _In_ sai_object_id_t switch_id);

            static void onSwitchStateChange(
                    _In_ sai_object_id_t switch_id,
                    _In_ sai_switch_oper_status_t switch_oper_status);

        private:

            /*
             * TODO: currently we can have only one notification handler (global),
             * but later on we can use templates to generate multiple static method
             * addresses on compile time and use that in slot factory.
             */

            static std::shared_ptr<NotificationHandler> m_handler;
    };
}
