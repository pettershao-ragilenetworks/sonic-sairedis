#pragma once

#include "RemoteSaiInterface.h"

#include "swss/producertable.h"
#include "swss/consumertable.h"

#include <memory>

/*
 * Asic state table commands. Those names are special and they will be used
 * inside swsscommon library LUA scripts to perfrom operations on redis
 * database.
 */

#define REDIS_ASIC_STATE_COMMAND_REMOVE "remove"
#define REDIS_ASIC_STATE_COMMAND_GETRESPONSE "getresponse"

/**
 * @brief Get response timeout in miliseconds.
 */
#define REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS (60*1000)

namespace sairedis
{
    class RedisRemoteSaiInterface:
        public RemoteSaiInterface
    {
        public:

            RedisRemoteSaiInterface(
                    _In_ std::shared_ptr<swss::ProducerTable> asicState,
                    _In_ std::shared_ptr<swss::ConsumerTable> getConsumer);

            virtual ~RedisRemoteSaiInterface() = default;

        public: // SAI interface overrides

            virtual sai_status_t remove(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId) override;

        private:

            sai_status_t remove(
                    _In_ const std::string& serializedObjectType,
                    _In_ const std::string& serializedObjectId);

            /**
             * @brief Wait for response.
             *
             * Will wait for reponse from syncd. Method used only for single
             * object create/remove/set since they have common output which is
             * sai_status_t.
             */
            sai_status_t waitForResponse(
                    _In_ sai_common_api_t api);

        private:

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
    };
}
