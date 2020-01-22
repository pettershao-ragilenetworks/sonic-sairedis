#include "Sai.h"
#include "SaiInternal.h"
#include "VirtualObjectIdManager.h"
#include "RedisVidIndexGenerator.h"
#include "RedisRemoteSaiInterface.h"
#include "WrapperRemoteSaiInterface.h"
#include "NotificationFactory.h"
#include "SwitchContainer.h"
#include "VirtualObjectIdManager.h"
#include "Recorder.h"
#include "RemoteSaiInterface.h"

#include "sairedis.h"
#include "sairediscommon.h"
#include "sai_redis.h"

#include "meta/Meta.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

#include "swss/logger.h"
#include "swss/selectableevent.h"
#include "swss/redisclient.h"
#include "swss/dbconnector.h"
#include "swss/producertable.h"
#include "swss/consumertable.h"
#include "swss/notificationconsumer.h"
#include "swss/notificationproducer.h"
#include "swss/table.h"
#include "swss/select.h"

#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>

#include <thread>
#include <algorithm>
#include <cstring>
#include <set>
#include <unordered_map>

// if we don't receive response from syncd in 60 seconds
// there is something wrong and we should fail
#define GET_RESPONSE_TIMEOUT (60*1000)

extern std::string getSelectResultAsString(int result);
extern void clear_local_state();
extern std::string joinFieldValues(
        _In_ const std::vector<swss::FieldValueTuple> &values);

extern sai_status_t internal_api_wait_for_response(
        _In_ sai_common_api_t api);

// other global declarations

bool g_recordStats = true;

using namespace sairedis;

volatile bool g_asicInitViewMode = false; // default mode is apply mode
volatile bool g_useTempView = false;
volatile bool g_syncMode = false;

using namespace sairedis;
using namespace saimeta;

sai_service_method_table_t g_services;
volatile bool          g_run = false;

std::shared_ptr<swss::DBConnector>          g_db;
std::shared_ptr<swss::ProducerTable>        g_asicState;
std::shared_ptr<swss::ConsumerTable>        g_redisGetConsumer;
std::shared_ptr<swss::RedisPipeline>        g_redisPipeline;

// TODO must be per syncd instance
std::shared_ptr<SwitchContainer>            g_switchContainer;
std::shared_ptr<VirtualObjectIdManager>     g_virtualObjectIdManager;
std::shared_ptr<RedisVidIndexGenerator>     g_redisVidIndexGenerator;
std::shared_ptr<Recorder>                   g_recorder;

#define REDIS_CHECK_API_INITIALIZED()                                       \
    if (!m_apiInitialized) {                                                \
        SWSS_LOG_ERROR("%s: api not initialized", __PRETTY_FUNCTION__);     \
        return SAI_STATUS_FAILURE; }

Sai::Sai()
{
    SWSS_LOG_ENTER();

    m_apiInitialized = false;
}

Sai::~Sai()
{
    SWSS_LOG_ENTER();

    if (m_apiInitialized)
    {
        uninitialize();
    }
}

// INITIALIZE UNINITIALIZE

sai_status_t Sai::initialize(
        _In_ uint64_t flags,
        _In_ const sai_service_method_table_t *service_method_table)
{
    MUTEX();

    SWSS_LOG_ENTER();

    if (m_apiInitialized)
    {
        SWSS_LOG_ERROR("%s: api already initialized", __PRETTY_FUNCTION__);

        return SAI_STATUS_FAILURE;
    }

    if (flags != 0)
    {
        SWSS_LOG_ERROR("invalid flags passed to SAI API initialize");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if ((service_method_table == NULL) ||
            (service_method_table->profile_get_next_value == NULL) ||
            (service_method_table->profile_get_value == NULL))
    {
        SWSS_LOG_ERROR("invalid service_method_table handle passed to SAI API initialize");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    memcpy(&m_service_method_table, service_method_table, sizeof(m_service_method_table));

    g_db                        = std::make_shared<swss::DBConnector>(ASIC_DB, swss::DBConnector::DEFAULT_UNIXSOCKET, 0);
    g_redisPipeline             = std::make_shared<swss::RedisPipeline>(g_db.get()); //enable default pipeline 128
    g_asicState                 = std::make_shared<swss::ProducerTable>(g_redisPipeline.get(), ASIC_STATE_TABLE, true);
    g_redisGetConsumer          = std::make_shared<swss::ConsumerTable>(g_db.get(), "GETRESPONSE");
    g_redisVidIndexGenerator    = std::make_shared<RedisVidIndexGenerator>(g_db, REDIS_KEY_VIDCOUNTER);

    g_recorder = std::make_shared<Recorder>();

    clear_local_state();

    g_asicInitViewMode = false;

    g_useTempView = false;

    // will create notification thread
    auto impl = std::make_shared<RedisRemoteSaiInterface>(
            g_asicState,
            g_redisGetConsumer,
            std::bind(&Sai::handle_notification, this, std::placeholders::_1));

    m_redisSai = std::make_shared<WrapperRemoteSaiInterface>(impl);

    m_meta = std::make_shared<Meta>(m_redisSai);

    m_apiInitialized = true;

    return SAI_STATUS_SUCCESS;
}

sai_status_t Sai::uninitialize(void)
{
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    // TODO: possible deadlock: user called uninitialize, and obtained lock and
    // call destructor on SAI interface, which in destructor will try to join
    // thread, but then notification arrived, and notification thread tries to
    // process it, and tries to acquire lock, but lock is taken by uninitialize

    m_redisSai= nullptr;
    m_meta = nullptr;

    g_recorder = nullptr;

    // clear everything after stopping notification thread
    clear_local_state();

    m_apiInitialized = false;

    return SAI_STATUS_SUCCESS;
}

// QUAD OID

sai_status_t Sai::create(
        _In_ sai_object_type_t objectType,
        _Out_ sai_object_id_t* objectId,
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->create(
            objectType,
            objectId,
            switchId,
            attr_count,
            attr_list);
}

sai_status_t Sai::remove(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->remove(objectType, objectId);
}

/*
 * NOTE: Notifications during switch create and switch remove.
 *
 * It is possible that when we create switch we will immediately start getting
 * notifications from it, and it may happen that this switch will not be yet
 * put to switch container and notification won't find it. But before
 * notification will be processed it will first try to acquire mutex, so create
 * switch function will end and switch will be put inside container.
 *
 * Similar it can happen that we receive notification when we are removing
 * switch, then switch will be removed from switch container and notification
 * will not find existing switch, but that's ok since switch was removed, and
 * notification can be ignored.
 */

sai_status_t Sai::set(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ const sai_attribute_t *attr)
{
    // SWSS_LOG_ENTER() omitted here, defined below after mutex TODO fix

    if (attr != NULL && attr->id == SAI_REDIS_SWITCH_ATTR_PERFORM_LOG_ROTATE)
    {
        /*
         * Let's avoid using mutexes, since this attribute could be used in
         * signal handler, so check it's value here. If set this attribute will
         * be performed from multiple threads there is possibility for race
         * condition here, but this doesn't matter since we only set logrotate
         * flag, and if that happens we will just reopen file less times then
         * actual set operation was called.
         */

        auto rec = g_recorder; // make local to keep reference

        if (rec)
        {
            rec->requestLogRotate();
        }

        return SAI_STATUS_SUCCESS;
    }

    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    if (objectType == SAI_OBJECT_TYPE_SWITCH && attr != NULL)
    {
        /*
         * NOTE: that this will work without
         * switch being created.
         */

        switch (attr->id)
        {
            case SAI_REDIS_SWITCH_ATTR_PERFORM_LOG_ROTATE:
                if (g_recorder)
                    g_recorder->requestLogRotate();
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_RECORD:
                if (g_recorder)
                    g_recorder->enableRecording(attr->value.booldata);
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_NOTIFY_SYNCD:
                return sai_redis_notify_syncd(objectId, attr);

            case SAI_REDIS_SWITCH_ATTR_USE_TEMP_VIEW:
                g_useTempView = attr->value.booldata;
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_RECORD_STATS:
                g_recordStats = attr->value.booldata;
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_SYNC_MODE:

                g_syncMode = attr->value.booldata;

                if (g_syncMode)
                {
                    SWSS_LOG_NOTICE("disabling buffered pipeline in sync mode");
                    g_asicState->setBuffered(false);
                }

                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_USE_PIPELINE:

                if (g_syncMode)
                {
                    SWSS_LOG_WARN("use pipeline is not supported in sync mode");
                    return SAI_STATUS_NOT_SUPPORTED;
                }

                g_asicState->setBuffered(attr->value.booldata);
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_FLUSH:
                g_asicState->flush();
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_RECORDING_OUTPUT_DIR:
                if (g_recorder && g_recorder->setRecordingOutputDirectory(*attr))
                    return SAI_STATUS_SUCCESS;
                return SAI_STATUS_FAILURE;

            default:
                break;
        }
    }

    return m_meta->set(objectType, objectId, attr);
}

sai_status_t Sai::get(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->get(
            objectType,
            objectId,
            attr_count,
            attr_list);
}

// QUAD ENTRY

#define DECLARE_CREATE_ENTRY(OT,ot)                         \
sai_status_t Sai::create(                                   \
        _In_ const sai_ ## ot ## _t* entry,                 \
        _In_ uint32_t attr_count,                           \
        _In_ const sai_attribute_t *attr_list)              \
{                                                           \
    MUTEX();                                                \
    SWSS_LOG_ENTER();                                       \
    REDIS_CHECK_API_INITIALIZED();                             \
    return m_meta->create(entry, attr_count, attr_list);    \
}

DECLARE_CREATE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_CREATE_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_CREATE_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_CREATE_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_CREATE_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_CREATE_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_CREATE_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_CREATE_ENTRY(NAT_ENTRY,nat_entry);


#define DECLARE_REMOVE_ENTRY(OT,ot)                         \
sai_status_t Sai::remove(                                   \
        _In_ const sai_ ## ot ## _t* entry)                 \
{                                                           \
    MUTEX();                                                \
    SWSS_LOG_ENTER();                                       \
    REDIS_CHECK_API_INITIALIZED();                             \
    return m_meta->remove(entry);                           \
}

DECLARE_REMOVE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_REMOVE_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_REMOVE_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_REMOVE_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_REMOVE_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_REMOVE_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_REMOVE_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_REMOVE_ENTRY(NAT_ENTRY,nat_entry);

#define DECLARE_SET_ENTRY(OT,ot)                            \
sai_status_t Sai::set(                                      \
        _In_ const sai_ ## ot ## _t* entry,                 \
        _In_ const sai_attribute_t *attr)                   \
{                                                           \
    MUTEX();                                                \
    SWSS_LOG_ENTER();                                       \
    REDIS_CHECK_API_INITIALIZED();                             \
    return m_meta->set(entry, attr);                        \
}

DECLARE_SET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_SET_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_SET_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_SET_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_SET_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_SET_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_SET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_SET_ENTRY(NAT_ENTRY,nat_entry);

#define DECLARE_GET_ENTRY(OT,ot)                            \
sai_status_t Sai::get(                                      \
        _In_ const sai_ ## ot ## _t* entry,                 \
        _In_ uint32_t attr_count,                           \
        _Inout_ sai_attribute_t *attr_list)                 \
{                                                           \
    MUTEX();                                                \
    SWSS_LOG_ENTER();                                       \
    REDIS_CHECK_API_INITIALIZED();                             \
    return m_meta->get(entry, attr_count, attr_list);       \
}

DECLARE_GET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_GET_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_GET_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_GET_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_GET_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_GET_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_GET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_GET_ENTRY(NAT_ENTRY,nat_entry);

// QUAD SERIALIZED

sai_status_t Sai::create(
        _In_ sai_object_type_t object_type,
        _In_ const std::string& serializedObjectId,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t Sai::remove(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& serializedObjectId)
{
    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t Sai::set(
        _In_ sai_object_type_t objectType,
        _In_ const std::string &serializedObjectId,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t Sai::get(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& serializedObjectId,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

// STATS

sai_status_t Sai::getStats(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids,
        _Out_ uint64_t *counters)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->getStats(
            object_type,
            object_id,
            number_of_counters,
            counter_ids,
            counters);
}

sai_status_t Sai::getStatsExt(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids,
        _In_ sai_stats_mode_t mode,
        _Out_ uint64_t *counters)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->getStatsExt(
            object_type,
            object_id,
            number_of_counters,
            counter_ids,
            mode,
            counters);
}

sai_status_t Sai::clearStats(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->clearStats(
            object_type,
            object_id,
            number_of_counters,
            counter_ids);
}

// BULK QUAD OID

sai_status_t Sai::bulkCreate(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t object_count,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_object_id_t *object_id,
        _Out_ sai_status_t *object_statuses)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->bulkCreate(
            object_type,
            switch_id,
            object_count,
            attr_count,
            attr_list,
            mode,
            object_id,
            object_statuses);
}

sai_status_t Sai::bulkRemove(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t object_count,
        _In_ const sai_object_id_t *object_id,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->bulkRemove(
            object_type,
            object_count,
            object_id,
            mode,
            object_statuses);
}

sai_status_t Sai::bulkSet(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t object_count,
        _In_ const sai_object_id_t *object_id,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->bulkSet(
            object_type,
            object_count,
            object_id,
            attr_list,
            mode,
            object_statuses);
}

// BULK QUAD SERIALIZED

sai_status_t Sai::bulkCreate(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Inout_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t Sai::bulkRemove(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t Sai::bulkSet(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

// BULK QUAD ENTRY

#define DECLARE_BULK_CREATE_ENTRY(OT,ot)                    \
sai_status_t Sai::bulkCreate(                               \
        _In_ uint32_t object_count,                         \
        _In_ const sai_ ## ot ## _t* entries,               \
        _In_ const uint32_t *attr_count,                    \
        _In_ const sai_attribute_t **attr_list,             \
        _In_ sai_bulk_op_error_mode_t mode,                 \
        _Out_ sai_status_t *object_statuses)                \
{                                                           \
    MUTEX();                                                \
    SWSS_LOG_ENTER();                                       \
    REDIS_CHECK_API_INITIALIZED();                             \
    return m_meta->bulkCreate(                              \
            object_count,                                   \
            entries,                                        \
            attr_count,                                     \
            attr_list,                                      \
            mode,                                           \
            object_statuses);                               \
}

DECLARE_BULK_CREATE_ENTRY(ROUTE_ENTRY,route_entry)
DECLARE_BULK_CREATE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_BULK_CREATE_ENTRY(NAT_ENTRY,nat_entry)


// BULK REMOVE

#define DECLARE_BULK_REMOVE_ENTRY(OT,ot)                    \
sai_status_t Sai::bulkRemove(                               \
        _In_ uint32_t object_count,                         \
        _In_ const sai_ ## ot ## _t *entries,               \
        _In_ sai_bulk_op_error_mode_t mode,                 \
        _Out_ sai_status_t *object_statuses)                \
{                                                           \
    MUTEX();                                                \
    SWSS_LOG_ENTER();                                       \
    REDIS_CHECK_API_INITIALIZED();                             \
    return m_meta->bulkRemove(                              \
            object_count,                                   \
            entries,                                        \
            mode,                                           \
            object_statuses);                               \
}

DECLARE_BULK_REMOVE_ENTRY(ROUTE_ENTRY,route_entry)
DECLARE_BULK_REMOVE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_BULK_REMOVE_ENTRY(NAT_ENTRY,nat_entry)

// BULK SET

#define DECLARE_BULK_SET_ENTRY(OT,ot)                       \
sai_status_t Sai::bulkSet(                                  \
        _In_ uint32_t object_count,                         \
        _In_ const sai_ ## ot ## _t *entries,               \
        _In_ const sai_attribute_t *attr_list,              \
        _In_ sai_bulk_op_error_mode_t mode,                 \
        _Out_ sai_status_t *object_statuses)                \
{                                                           \
    MUTEX();                                                \
    SWSS_LOG_ENTER();                                       \
    REDIS_CHECK_API_INITIALIZED();                             \
    return m_meta->bulkSet(                                 \
            object_count,                                   \
            entries,                                        \
            attr_list,                                      \
            mode,                                           \
            object_statuses);                               \
}

DECLARE_BULK_SET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_BULK_SET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_BULK_SET_ENTRY(NAT_ENTRY,nat_entry);

// NON QUAD API

sai_status_t Sai::flushFdbEntries(
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->flushFdbEntries(
            switch_id,
            attr_count,
            attr_list);
}

// SAI API

sai_status_t Sai::objectTypeGetAvailability(
        _In_ sai_object_id_t switchId,
        _In_ sai_object_type_t objectType,
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList,
        _Out_ uint64_t *count)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->objectTypeGetAvailability(
            switchId,
            objectType,
            attrCount,
            attrList,
            count);
}

sai_status_t Sai::queryAattributeEnumValuesCapability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ sai_attr_id_t attr_id,
        _Inout_ sai_s32_list_t *enum_values_capability)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return m_meta->queryAattributeEnumValuesCapability(
            switch_id,
            object_type,
            attr_id,
            enum_values_capability);
}

sai_object_type_t Sai::objectTypeQuery(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    if (!m_apiInitialized)
    {
        SWSS_LOG_ERROR("%s: SAI API not initialized", __PRETTY_FUNCTION__);

        return SAI_OBJECT_TYPE_NULL;
    }

    // not need for metadata check or mutex since this method is static

    return VirtualObjectIdManager::objectTypeQuery(objectId);
}

sai_object_id_t Sai::switchIdQuery(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    if (!m_apiInitialized)
    {
        SWSS_LOG_ERROR("%s: SAI API not initialized", __PRETTY_FUNCTION__);

        return SAI_NULL_OBJECT_ID;
    }

    // not need for metadata check or mutex since this method is static

    return VirtualObjectIdManager::switchIdQuery(objectId);
}

std::string joinFieldValues(
        _In_ const std::vector<swss::FieldValueTuple> &values)
{
    SWSS_LOG_ENTER();

    std::stringstream ss;

    for (size_t i = 0; i < values.size(); ++i)
    {
        const std::string &str_attr_id = fvField(values[i]);
        const std::string &str_attr_value = fvValue(values[i]);

        if(i != 0)
        {
            ss << "|";
        }

        ss << str_attr_id << "=" << str_attr_value;
    }

    return ss.str();
}

/*
 * Max number of counters used in 1 api call
 */
#define REDIS_MAX_COUNTERS 128

#define REDIS_COUNTERS_COUNT_MSB (0x80000000)


std::vector<swss::FieldValueTuple> serialize_counter_id_list(
        _In_ const sai_enum_metadata_t *stats_enum,
        _In_ uint32_t count,
        _In_ const sai_stat_id_t*counter_id_list)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> values;

    for (uint32_t i = 0; i < count; i++)
    {
        const char *name = sai_metadata_get_enum_value_name(stats_enum, counter_id_list[i]);

        if (name == NULL)
        {
            SWSS_LOG_THROW("failed to find enum %d in %s", counter_id_list[i], stats_enum->name);
        }

        values.emplace_back(name, "");
    }

    return values;
}


sai_status_t internal_redis_bulk_generic_set(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _In_ const sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::string str_object_type = sai_serialize_object_type(object_type);

    std::vector<swss::FieldValueTuple> entries;
    std::vector<swss::FieldValueTuple> entriesWithStatus;

    /*
     * We are recording all entries and their statuses, but we send to sairedis
     * only those that succeeded metadata check, since only those will be
     * executed on syncd, so there is no need with bothering decoding statuses
     * on syncd side.
     */

    for (size_t idx = 0; idx < serialized_object_ids.size(); ++idx)
    {
        std::vector<swss::FieldValueTuple> entry =
            SaiAttributeList::serialize_attr_list(object_type, 1, &attr_list[idx], false);

        std::string str_attr = joinFieldValues(entry);

        std::string str_status = sai_serialize_status(object_statuses[idx]);

        std::string joined = str_attr + "|" + str_status;

        swss::FieldValueTuple fvt(serialized_object_ids[idx] , joined);

        entriesWithStatus.push_back(fvt);

        if (object_statuses[idx] != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_WARN("skipping %s since status is %s",
                    serialized_object_ids[idx].c_str(),
                    str_status.c_str());

            continue;
        }
    }

    return SAI_STATUS_FAILURE;
}


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

sai_switch_notifications_t Sai::processNotification(
        _In_ std::shared_ptr<Notification> notification)
{
    MUTEX();
    SWSS_LOG_ENTER();

    if (!m_apiInitialized)
    {
        SWSS_LOG_ERROR("%s: api not initialized", __PRETTY_FUNCTION__);

        return { };
    }

    // TODO it may happen that at this point sai will be already uninitialized
    // since uninitialize is not waiting for processNotification to wait

    // NOTE: process metadata must be executed under sairedis API mutex since
    // it will access meta database and notification comes from different
    // thread, and this method is executed from notifications thread

    if (!m_meta)
    {
        SWSS_LOG_WARN("meta is already null");

        return { };
    }

    notification->processMetadata(m_meta);

    auto objectId = notification->getAnyObjectId();

    auto switchId = g_virtualObjectIdManager->saiSwitchIdQuery(objectId);

    return getSwitchNotifications(switchId);
}

void Sai::handle_notification(
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

sai_status_t Sai::sai_redis_notify_syncd(
        _In_ sai_object_id_t switchId,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    auto redisNotifySyncd = (sai_redis_notify_syncd_t)attr->value.s32;

    switch(redisNotifySyncd)
    {
        case SAI_REDIS_NOTIFY_SYNCD_INIT_VIEW:
        case SAI_REDIS_NOTIFY_SYNCD_APPLY_VIEW:
        case SAI_REDIS_NOTIFY_SYNCD_INSPECT_ASIC:
            break;

        default:

            SWSS_LOG_ERROR("invalid notify syncd attr value %s", sai_serialize(redisNotifySyncd).c_str());

            return SAI_STATUS_FAILURE;
    }

    auto status = m_redisSai->notifySyncd(switchId, redisNotifySyncd);

    if (status == SAI_STATUS_SUCCESS)
    {
        switch (redisNotifySyncd)
        {
            case SAI_REDIS_NOTIFY_SYNCD_INIT_VIEW:

                SWSS_LOG_NOTICE("switched ASIC to INIT VIEW");

                g_asicInitViewMode = true;

                SWSS_LOG_NOTICE("clearing current local state since init view is called on initialized switch");

                // TODO this must be per syncd instance
                clear_local_state();

                break;

            case SAI_REDIS_NOTIFY_SYNCD_APPLY_VIEW:

                SWSS_LOG_NOTICE("switched ASIC to APPLY VIEW");

                g_asicInitViewMode = false;

                break;

            case SAI_REDIS_NOTIFY_SYNCD_INSPECT_ASIC:

                SWSS_LOG_NOTICE("inspec ASIC SUCCEEDED");

                break;

            default:
                break;
        }
    }

    return status;
}

void Sai::clear_local_state()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("clearing local state");

    // Will need to be executed after init VIEW

    // will clear switch container
    g_switchContainer = std::make_shared<SwitchContainer>();

    // TODO since we create new manager, we need to create new meta db with
    // updated functions for query object type and switch id
    // TODO update global context when supporting multiple syncd instances
    g_virtualObjectIdManager = std::make_shared<VirtualObjectIdManager>(0, g_redisVidIndexGenerator);

    // Initialize metadata database.
    // TODO must be done per syncd instance
    if (m_meta)
        m_meta->meta_init_db();
}



