#pragma once

#include "RemoteSaiInterface.h"

#include "swss/producertable.h"
#include "swss/consumertable.h"

#include <memory>

/*
 * Asic state table commands. Those names are special and they will be used
 * inside swsscommon library LUA scripts to perform operations on redis
 * database.
 */

#define REDIS_ASIC_STATE_COMMAND_CREATE "create"
#define REDIS_ASIC_STATE_COMMAND_REMOVE "remove"
#define REDIS_ASIC_STATE_COMMAND_SET    "set"
#define REDIS_ASIC_STATE_COMMAND_GET    "get"

#define REDIS_ASIC_STATE_COMMAND_GETRESPONSE        "getresponse"
#define REDIS_ASIC_STATE_COMMAND_FLUSHRESPONSE      "flushresponse"

/**
 * @brief Get response timeout in milliseconds.
 */
#define REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS (60*1000)

#define SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(ot)   \
    virtual sai_status_t remove(                                    \
            _In_ const sai_ ## ot ## _t* ot) override;

#define SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(ot)   \
    virtual sai_status_t create(                                    \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ uint32_t attr_count,                               \
            _In_ const sai_attribute_t *attr_list) override;

#define SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(ot)      \
    virtual sai_status_t set(                                       \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ const sai_attribute_t *attr) override;

#define SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(ot)      \
    virtual sai_status_t get(                                       \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ uint32_t attr_count,                               \
            _Out_ sai_attribute_t *attr_list) override;

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

            virtual sai_status_t create(
                    _In_ sai_object_type_t objectType,
                    _Out_ sai_object_id_t* objectId,
                    _In_ sai_object_id_t switchId,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list) override;

            virtual sai_status_t remove(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId) override;

            virtual sai_status_t set(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId,
                    _In_ const sai_attribute_t *attr) override;

            virtual sai_status_t get(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId,
                    _In_ uint32_t attr_count,
                    _Inout_ sai_attribute_t *attr_list) override;

        public: // create ENTRY

            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(inseg_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(ipmc_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(l2mc_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(mcast_fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(neighbor_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(route_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(nat_entry);

        public: // remove ENTRY

            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(inseg_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(ipmc_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(l2mc_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(mcast_fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(neighbor_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(route_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(nat_entry);

        public: // set ENTRY

            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(inseg_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(ipmc_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(l2mc_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(mcast_fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(neighbor_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(route_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_SET_ENTRY(nat_entry);

        public: // get ENTRY

            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(inseg_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(ipmc_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(l2mc_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(mcast_fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(neighbor_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(route_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_GET_ENTRY(nat_entry);

        public:

            virtual sai_status_t flushFdbEntries(
                    _In_ sai_object_id_t switchId,
                    _In_ uint32_t attrCount,
                    _In_ const sai_attribute_t *attrList) override;

        private:

            sai_status_t create(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& serializedObjectId,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            sai_status_t remove(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& serializedObjectId);

            sai_status_t set(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& serializedObjectId,
                    _In_ const sai_attribute_t *attr);

            sai_status_t get(
                    _In_ sai_object_type_t objectType,
                    _In_ const std::string& serializedObjectId,
                    _In_ uint32_t attr_count,
                    _Inout_ sai_attribute_t *attr_list);

            /**
             * @brief Wait for response.
             *
             * Will wait for response from syncd. Method used only for single
             * object create/remove/set since they have common output which is
             * sai_status_t.
             */
            sai_status_t waitForResponse(
                    _In_ sai_common_api_t api);

            /**
             * @brief Wait for GET response.
             *
             * Will wait for response from syncd. Method only used for single
             * GET object. If status is SUCCESS all values will be deserialized
             * and transferred to user buffers. If status is BUFFER_OVERFLOW
             * then all non list values will be transferred, but LIST objects
             * will only transfer COUNT item of list, without touching user
             * list at all.
             */
            sai_status_t waitForGetResponse(
                    _In_ sai_object_type_t objectType,
                    _In_ uint32_t attr_count,
                    _Inout_ sai_attribute_t *attr_list);

            sai_status_t waitForFlushFdbEntriesResponse();

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
