#pragma once

#include "RemoteSaiInterface.h"
#include "Notification.h"

#include "swss/producertable.h"
#include "swss/consumertable.h"
#include "swss/notificationconsumer.h"
#include "swss/selectableevent.h"

#include <memory>
#include <functional>

/*
 * Asic state table commands. Those names are special and they will be used
 * inside swsscommon library LUA scripts to perform operations on redis
 * database.
 */

#define REDIS_ASIC_STATE_COMMAND_CREATE "create"
#define REDIS_ASIC_STATE_COMMAND_REMOVE "remove"
#define REDIS_ASIC_STATE_COMMAND_SET    "set"
#define REDIS_ASIC_STATE_COMMAND_GET    "get"

#define REDIS_ASIC_STATE_COMMAND_BULK_CREATE "bulkcreate"
#define REDIS_ASIC_STATE_COMMAND_BULK_REMOVE "bulkremove"
#define REDIS_ASIC_STATE_COMMAND_BULK_SET    "bulkset"
#define REDIS_ASIC_STATE_COMMAND_BULK_GET    "bulkget"

#define REDIS_ASIC_STATE_COMMAND_NOTIFY      "notify"

#define REDIS_ASIC_STATE_COMMAND_GET_STATS          "get_stats"
#define REDIS_ASIC_STATE_COMMAND_CLEAR_STATS        "clear_stats"

#define REDIS_ASIC_STATE_COMMAND_GETRESPONSE        "getresponse"

#define REDIS_ASIC_STATE_COMMAND_FLUSH              "flush"
#define REDIS_ASIC_STATE_COMMAND_FLUSHRESPONSE      "flushresponse"

#define REDIS_ASIC_STATE_COMMAND_ATTR_ENUM_VALUES_CAPABILITY_QUERY      "attr_enum_values_capability_query"
#define REDIS_ASIC_STATE_COMMAND_ATTR_ENUM_VALUES_CAPABILITY_RESPONSE   "attr_enum_values_capability_response"

#define REDIS_ASIC_STATE_COMMAND_OBJECT_TYPE_GET_AVAILABILITY_QUERY     "object_type_get_availability_query"
#define REDIS_ASIC_STATE_COMMAND_OBJECT_TYPE_GET_AVAILABILITY_RESPONSE  "object_type_get_availability_response"

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

#define SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_CREATE_ENTRY(ot)  \
    virtual sai_status_t bulkCreate(                                    \
            _In_ uint32_t object_count,                                 \
            _In_ const sai_ ## ot ## _t *ot,                            \
            _In_ const uint32_t *attr_count,                            \
            _In_ const sai_attribute_t **attr_list,                     \
            _In_ sai_bulk_op_error_mode_t mode,                         \
            _Out_ sai_status_t *object_statuses) override;

#define SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_REMOVE_ENTRY(ot)  \
    virtual sai_status_t bulkRemove(                                    \
            _In_ uint32_t object_count,                                 \
            _In_ const sai_ ## ot ## _t *ot,                            \
            _In_ sai_bulk_op_error_mode_t mode,                         \
            _Out_ sai_status_t *object_statuses) override;

#define SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_SET_ENTRY(ot)     \
    virtual sai_status_t bulkSet(                                       \
            _In_ uint32_t object_count,                                 \
            _In_ const sai_ ## ot ## _t *ot,                            \
            _In_ const sai_attribute_t *attr_list,                      \
            _In_ sai_bulk_op_error_mode_t mode,                         \
            _Out_ sai_status_t *object_statuses) override;

namespace sairedis
{
    class RedisRemoteSaiInterface:
        public RemoteSaiInterface
    {
        public:

            RedisRemoteSaiInterface(
                    _In_ std::shared_ptr<swss::ProducerTable> asicState,
                    _In_ std::shared_ptr<swss::ConsumerTable> getConsumer,
                    _In_ std::function<void(std::shared_ptr<Notification>)> notificationCallback);

            virtual ~RedisRemoteSaiInterface();

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

        public: // bulk QUAD oid

            virtual sai_status_t bulkCreate(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t object_count,
                    _In_ const uint32_t *attr_count,
                    _In_ const sai_attribute_t **attr_list,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Out_ sai_object_id_t *object_id,
                    _Out_ sai_status_t *object_statuses) override;

            virtual sai_status_t bulkRemove(
                    _In_ sai_object_type_t object_type,
                    _In_ uint32_t object_count,
                    _In_ const sai_object_id_t *object_id,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Out_ sai_status_t *object_statuses) override;

            virtual sai_status_t bulkSet(
                    _In_ sai_object_type_t object_type,
                    _In_ uint32_t object_count,
                    _In_ const sai_object_id_t *object_id,
                    _In_ const sai_attribute_t *attr_list,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Out_ sai_status_t *object_statuses) override;

        public: // bulk create ENTRY

            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_CREATE_ENTRY(fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_CREATE_ENTRY(nat_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_CREATE_ENTRY(route_entry);

        public: // bulk remove ENTRY

            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_REMOVE_ENTRY(fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_REMOVE_ENTRY(nat_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_REMOVE_ENTRY(route_entry);

        public: // bulk set ENTRY

            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_SET_ENTRY(fdb_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_SET_ENTRY(nat_entry);
            SAIREDIS_REDISREMOTESAIINTERFACE_DECLARE_BULK_SET_ENTRY(route_entry);

        public: // stats API

            virtual sai_status_t getStats(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _In_ uint32_t number_of_counters,
                    _In_ const sai_stat_id_t *counter_ids,
                    _Out_ uint64_t *counters) override;

            virtual sai_status_t getStatsExt(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _In_ uint32_t number_of_counters,
                    _In_ const sai_stat_id_t *counter_ids,
                    _In_ sai_stats_mode_t mode,
                    _Out_ uint64_t *counters) override;

            virtual sai_status_t clearStats(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _In_ uint32_t number_of_counters,
                    _In_ const sai_stat_id_t *counter_ids) override;

        public: // non QUAD API

            virtual sai_status_t flushFdbEntries(
                    _In_ sai_object_id_t switchId,
                    _In_ uint32_t attrCount,
                    _In_ const sai_attribute_t *attrList) override;

        public: // SAI API

            virtual sai_status_t objectTypeGetAvailability(
                    _In_ sai_object_id_t switchId,
                    _In_ sai_object_type_t objectType,
                    _In_ uint32_t attrCount,
                    _In_ const sai_attribute_t *attrList,
                    _Out_ uint64_t *count) override;

            virtual sai_status_t queryAattributeEnumValuesCapability(
                    _In_ sai_object_id_t switch_id,
                    _In_ sai_object_type_t object_type,
                    _In_ sai_attr_id_t attr_id,
                    _Inout_ sai_s32_list_t *enum_values_capability) override;

        public: // notify syncd

            virtual sai_status_t notifySyncd(
                    _In_ sai_object_id_t switchId,
                    _In_ sai_redis_notify_syncd_t redisNotifySyncd) override;

        public:

            /**
             * @brief Checks whether attribute is custom SAI_REDIS_SWITCH attribute.
             *
             * This function should only be used on switch_api set function.
             */
            static bool isRedisAttribute(
                    _In_ const sai_attribute_t* attr);

            sai_status_t setRedisAttribute(
                    _In_ sai_object_id_t switchId,
                    _In_ const sai_attribute_t* attr);

        private: // QUAD API helpers

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

        private: // bulk QUAD API helpers

            sai_status_t bulkCreate(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::vector<std::string> &serialized_object_ids,
                    _In_ const uint32_t *attr_count,
                    _In_ const sai_attribute_t **attr_list,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Inout_ sai_status_t *object_statuses);

            sai_status_t bulkRemove(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::vector<std::string> &serialized_object_ids,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Out_ sai_status_t *object_statuses);

            sai_status_t bulkSet(
                    _In_ sai_object_type_t object_type,
                    _In_ const std::vector<std::string> &serialized_object_ids,
                    _In_ const sai_attribute_t *attr_list,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Out_ sai_status_t *object_statuses);

        private: // QUAD API response

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

        private: // bulk QUAD API response

            /**
             * @brief Wait for bulk response.
             *
             * Will wait for response from syncd. Method used only for bulk
             * object create/remove/set since they have common output which is
             * sai_status_t and object_statuses.
             */
            sai_status_t waitForBulkResponse(
                    _In_ sai_common_api_t api,
                    _In_ uint32_t object_count,
                    _Out_ sai_status_t *object_statuses);

        private: // stats API response

            sai_status_t waitForGetStatsResponse(
                    _In_ uint32_t number_of_counters,
                    _Out_ uint64_t *counters);

            sai_status_t waitForClearStatsResponse();

        private: // non QUAD API response

            sai_status_t waitForFlushFdbEntriesResponse();

        private: // SAI API response

            sai_status_t waitForQueryAattributeEnumValuesCapabilityResponse(
                    _Inout_ sai_s32_list_t* enumValuesCapability);

            sai_status_t waitForObjectTypeGetAvailabilityResponse(
                    _In_ uint64_t *count);

        private: // notify syncd response

            sai_status_t waitForNotifySyncdResponse();

        private: // helpers

            // TODO to be removed when swss-common pointer will be advanced
            static std::string getSelectResultAsString(int result);

        private: // notification

            void notificationThreadFunction();

            void handleNotification(
                    _In_ const std::string &name,
                    _In_ const std::string &serializedNotification,
                    _In_ const std::vector<swss::FieldValueTuple> &values);

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

        private: // notification

            std::function<void(std::shared_ptr<Notification>)> m_notificationCallback;

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
