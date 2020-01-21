#include "Sai.h"
#include "SaiInternal.h"

#include "swss/logger.h"

#include "sairedis.h"

#include <unistd.h>
#include <inttypes.h>

#include <algorithm>
#include <cstring>

using namespace sairedis;

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

    auto switch_type = service_method_table->profile_get_value(0, SAI_KEY_VS_SWITCH_TYPE);

    if (switch_type == NULL)
    {
        SWSS_LOG_ERROR("failed to obtain service method table value: %s", SAI_KEY_VS_SWITCH_TYPE);

        return SAI_STATUS_FAILURE;
    }

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
            std::bind<void(std::shared_ptr<Notification>)>(&handle_notification, std::placeholders::_1)); // possible tcp/shmem interfaces

    g_remoteSaiInterface = std::make_shared<WrapperRemoteSaiInterface>(impl);

    g_meta = std::make_shared<Meta>(g_remoteSaiInterface);

    Globals::apiInitialized = true;

    return SAI_STATUS_SUCCESS;

    //auto sc = std::make_shared<SwitchConfig>();

    //sc->m_switchType = switchType;
    //sc->m_bootType = bootType;
    //sc->m_switchIndex = 0;
    //sc->m_useTapDevice = useTapDevice;
    //sc->m_laneMap = m_laneMapContainer->getLaneMap(sc->m_switchIndex);
    //sc->m_eventQueue = m_eventQueue;

    //auto scc = std::make_shared<SwitchConfigContainer>();

    //scc->insert(sc);

    //m_vsSai = std::make_shared<VirtualSwitchSaiInterface>(scc);

    // m_meta = std::make_shared<saimeta::Meta>(m_vsSai);

    //m_vsSai->setMeta(m_meta);

    // startFdbAgingThread();

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

    g_meta = nullptr;

    // will stop notification thread
    g_remoteSaiInterface = nullptr;

    g_recorder = nullptr;

    // clear everything after stopping notification thread
    clear_local_state();

    //m_vsSai = nullptr;
    //m_meta = nullptr;

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

sai_status_t Sai::set(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ const sai_attribute_t *attr)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    if (objectType == SAI_OBJECT_TYPE_SWITCH)
    {
        if (attr)
        {
            if (attr->id == SAI_VS_SWITCH_ATTR_META_ENABLE_UNITTESTS)
            {
                m_meta->meta_unittests_enable(attr->value.booldata);
                return SAI_STATUS_SUCCESS;
            }

            if (attr->id == SAI_VS_SWITCH_ATTR_META_ALLOW_READ_ONLY_ONCE)
            {
                return m_meta->meta_unittests_allow_readonly_set_once(SAI_OBJECT_TYPE_SWITCH, attr->value.s32);
            }
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

    return RealObjectIdManager::objectTypeQuery(objectId);
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

    return RealObjectIdManager::switchIdQuery(objectId);
}

