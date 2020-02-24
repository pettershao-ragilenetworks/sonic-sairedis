#pragma once

#include "RemoteSaiInterface.h"
#include "SwitchContainer.h"
#include "VirtualObjectIdManager.h"
#include "Notification.h"
#include "Recorder.h"
#include "SkipRecordAttrContainer.h"

#include "swss/producertable.h"
#include "swss/consumertable.h"
#include "swss/notificationconsumer.h"
#include "swss/selectableevent.h"

#include <memory>
#include <functional>

namespace sairedis
{
    class RedisChannel
    {
        public:

            typedef std::function<void(const std::string&,const std::string&, const std::vector<swss::FieldValueTuple>&)> Callback;

        public:

            RedisChannel(
                    _In_ const std::string& dbAsic,
                    _In_ Callback callback);

            virtual ~RedisChannel();

        public:

            std::shared_ptr<swss::DBConnector> getDbConnector() const;

            void setBuffered(
                    _In_ bool buffered);

            void flush();

            void set(
                    _In_ const std::string& key, 
                    _In_ const std::vector<swss::FieldValueTuple>& values,
                    _In_ const std::string& command);

            void del(
                    _In_ const std::string& key,
                    _In_ const std::string& command);

            sai_status_t wait(
                    _In_ const std::string& command,
                    _Out_ swss::KeyOpFieldsValuesTuple& kco);

        private:

            void notificationThreadFunction();

        private:

            Callback m_callback;

        private:

            std::string m_dbAsic;

            /**
             * @brief Asic state channel.
             *
             * Used to sent commands like create/remove/set/get to syncd.
             */
            std::shared_ptr<swss::ProducerTable>  m_asicState;

            /**
             * @brief Get consumer.
             *
             * Channel used to receive responses from syncd.
             */
            std::shared_ptr<swss::ConsumerTable> m_getConsumer;

            std::shared_ptr<swss::DBConnector> m_db;

            std::shared_ptr<swss::RedisPipeline> m_redisPipeline;

        private: // notification

            /**
             * @brief Indicates whether notification thread should be running.
             */
            volatile bool m_runNotificationThread;

            /**
             * @brief Database connector used for notifications.
             */
            std::shared_ptr<swss::DBConnector> m_dbNtf;

            /**
             * @brief Notification consumer.
             */
            std::shared_ptr<swss::NotificationConsumer> m_notificationConsumer;

            /**
             * @brief Event used to nice end notifications thread.
             */
            swss::SelectableEvent m_notificationThreadShouldEndEvent;

            /**
             * @brief Notification thread
             */
            std::shared_ptr<std::thread> m_notificationThread;
    };
}
