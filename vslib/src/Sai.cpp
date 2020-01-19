#include "Sai.h"

#include "swss/logger.h"

#include "saivs.h"
#include "sai_vs.h"
#include "sai_vs_internal.h"

#include "swss/notificationconsumer.h"
#include "swss/select.h"

#include "RealObjectIdManager.h"
#include "VirtualSwitchSaiInterface.h"
#include "SwitchStateBase.h"
#include "LaneMapFileParser.h"
#include "HostInterfaceInfo.h"

#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include <algorithm>

using namespace saivs;

bool                    g_vs_hostif_use_tap_device = false;
sai_vs_switch_type_t    g_vs_switch_type = SAI_VS_SWITCH_TYPE_NONE;

std::shared_ptr<swss::SelectableEvent>      g_fdbAgingThreadEvent;
volatile bool                               g_fdbAgingThreadRun;
std::shared_ptr<std::thread>                g_fdbAgingThread;

sai_vs_boot_type_t g_vs_boot_type = SAI_VS_BOOT_TYPE_COLD;

std::shared_ptr<LaneMapContainer> g_laneMapContainer;

std::shared_ptr<RealObjectIdManager>            g_realObjectIdManager;

const char *g_boot_type             = NULL;

void processFdbEntriesForAging()
{
    SWSS_LOG_ENTER();

    if (!Globals::apimutex.try_lock())
    {
        // if we are under mutex when calling uninitialize
        // and doing thread join, this can cause deadlock if
        // this will kick in, so try lock instead of mutex guard.
        return;
    }

    // process for all switches

    for (auto& it: g_switch_state_map)
    {
        // TODO remove cast
        std::dynamic_pointer_cast<SwitchStateBase>(it.second)->processFdbEntriesForAging();
    }

    Globals::apimutex.unlock();
}

/**
 * @brief FDB aging thread timeout in milliseconds.
 *
 * Every timeout aging FDB will be performed.
 */
#define FDB_AGING_THREAD_TIMEOUT_MS (1000)

void fdbAgingThreadProc()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("starting fdb aging thread");

    swss::Select s;

    s.addSelectable(g_fdbAgingThreadEvent.get());

    while (g_fdbAgingThreadRun)
    {
        swss::Selectable *sel = nullptr;

        int result = s.select(&sel, FDB_AGING_THREAD_TIMEOUT_MS);

        if (sel == g_fdbAgingThreadEvent.get())
        {
            // user requested shutdown_switch
            break;
        }

        if (result == swss::Select::TIMEOUT)
        {
            processFdbEntriesForAging();
        }
    }

    SWSS_LOG_NOTICE("ending fdb aging thread");
}

Sai::Sai()
{
    SWSS_LOG_ENTER();

    m_unittestChannelRun = false;
}

Sai::~Sai()
{
    SWSS_LOG_ENTER();

    if (Globals::apiInitialized)
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

    if (Globals::apiInitialized)
    {
        SWSS_LOG_ERROR("%s: api already initialized", __PRETTY_FUNCTION__);

        return SAI_STATUS_FAILURE;
    }

    if ((service_method_table == NULL) ||
            (service_method_table->profile_get_next_value == NULL) ||
            (service_method_table->profile_get_value == NULL))
    {
        SWSS_LOG_ERROR("invalid service_method_table handle passed to SAI API initialize");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    memcpy(&m_service_method_table, service_method_table, sizeof(m_service_method_table));

    // TODO maybe this query should be done right before switch create
    const char *type = service_method_table->profile_get_value(0, SAI_KEY_VS_SWITCH_TYPE);

    if (type == NULL)
    {
        SWSS_LOG_ERROR("failed to obtain service method table value: %s", SAI_KEY_VS_SWITCH_TYPE);

        return SAI_STATUS_FAILURE;
    }

    auto *laneMapFile = service_method_table->profile_get_value(0, SAI_KEY_VS_INTERFACE_LANE_MAP_FILE);

    g_laneMapContainer = LaneMapFileParser::parseLaneMapFile(laneMapFile);

    g_boot_type             = service_method_table->profile_get_value(0, SAI_KEY_BOOT_TYPE);
    m_warm_boot_read_file   = service_method_table->profile_get_value(0, SAI_KEY_WARM_BOOT_READ_FILE);
    m_warm_boot_write_file  = service_method_table->profile_get_value(0, SAI_KEY_WARM_BOOT_WRITE_FILE);

    std::string bt = (g_boot_type == NULL) ? "cold" : g_boot_type;

    if (bt == "cold" || bt == SAI_VALUE_VS_BOOT_TYPE_COLD)
    {
        g_vs_boot_type = SAI_VS_BOOT_TYPE_COLD;
    }
    else if (bt == "warm" || bt == SAI_VALUE_VS_BOOT_TYPE_WARM)
    {
        g_vs_boot_type = SAI_VS_BOOT_TYPE_WARM;
    }
    else if (bt == "fast" || bt == SAI_VALUE_VS_BOOT_TYPE_COLD)
    {
        g_vs_boot_type = SAI_VS_BOOT_TYPE_FAST;
    }
    else
    {
        SWSS_LOG_ERROR("unsupported boot type: %s", g_boot_type);

        return SAI_STATUS_FAILURE;
    }

    std::string strType = type;

    if (strType == SAI_VALUE_VS_SWITCH_TYPE_BCM56850)
    {
        g_vs_switch_type = SAI_VS_SWITCH_TYPE_BCM56850;
    }
    else if (strType == SAI_VALUE_VS_SWITCH_TYPE_MLNX2700)
    {
        g_vs_switch_type = SAI_VS_SWITCH_TYPE_MLNX2700;
    }
    else
    {
        SWSS_LOG_ERROR("unknown switch type: '%s'", type);

        return SAI_STATUS_FAILURE;
    }

    const char *use_tap_dev = service_method_table->profile_get_value(0, SAI_KEY_VS_HOSTIF_USE_TAP_DEVICE);

    g_vs_hostif_use_tap_device = use_tap_dev != NULL && strcmp(use_tap_dev, "true") == 0;

    SWSS_LOG_NOTICE("hostif use TAP device: %s",
            g_vs_hostif_use_tap_device ? "true" : "false");

    if (flags != 0)
    {
        SWSS_LOG_ERROR("invalid flags passed to SAI API initialize");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    startUnittestThread();

    g_fdbAgingThreadEvent = std::make_shared<swss::SelectableEvent>();

    g_fdbAgingThreadRun = true;

    // TODO should this be moved to create switch and SwitchState?
    g_fdbAgingThread = std::make_shared<std::thread>(std::thread(fdbAgingThreadProc));

    // most important

    g_realObjectIdManager = std::make_shared<RealObjectIdManager>(0);

    g_switch_state_map.clear();

    m_vsSai = std::make_shared<VirtualSwitchSaiInterface>();

    m_meta = std::make_shared<saimeta::Meta>(m_vsSai);

    m_vsSai->setMeta(m_meta);

    if (g_vs_boot_type == SAI_VS_BOOT_TYPE_WARM)
    {
        if (!m_vsSai->readWarmBootFile(m_warm_boot_read_file))
        {
            SWSS_LOG_WARN("failed to read warm boot read file, switching to COLD BOOT");

            g_vs_boot_type = SAI_VS_BOOT_TYPE_COLD;
        }
    }

    Globals::apiInitialized = true;

    return SAI_STATUS_SUCCESS;
}

sai_status_t Sai::uninitialize(void)
{
    SWSS_LOG_ENTER();
    VS_CHECK_API_INITIALIZED();

    // no mutex on uninitialized to prevent deadlock
    // if some thread would try to gram api mutex

    SWSS_LOG_NOTICE("stopping threads");

    stopUnittestThread();

    g_fdbAgingThreadEvent->notify();

    g_fdbAgingThreadRun = false;

    g_fdbAgingThread->join();

    // clear state after ending all threads

    m_vsSai->writeWarmBootFile(m_warm_boot_write_file);

    m_vsSai = nullptr;
    m_meta = nullptr;

    g_switch_state_map.clear();

    // TODO since we create new manager, we need to create new meta db with
    // updated functions for query object type and switch id
    g_realObjectIdManager = nullptr;

    Globals::apiInitialized = false;

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
    VS_CHECK_API_INITIALIZED();

    // TODO switch is special case

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
    VS_CHECK_API_INITIALIZED();

    // TODO switch is special case

    return m_meta->remove(objectType, objectId);
}

sai_status_t Sai::set(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ const sai_attribute_t *attr)
{
    MUTEX();
    SWSS_LOG_ENTER();
    VS_CHECK_API_INITIALIZED();

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
    VS_CHECK_API_INITIALIZED();

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
    MUTEX();                                             \
    SWSS_LOG_ENTER();                                       \
    VS_CHECK_API_INITIALIZED();                                \
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
    MUTEX();                                             \
    SWSS_LOG_ENTER();                                       \
    VS_CHECK_API_INITIALIZED();                                \
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
    MUTEX();                                             \
    SWSS_LOG_ENTER();                                       \
    VS_CHECK_API_INITIALIZED();                                \
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
    MUTEX();                                             \
    SWSS_LOG_ENTER();                                       \
    VS_CHECK_API_INITIALIZED();                                \
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
    VS_CHECK_API_INITIALIZED();

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
    VS_CHECK_API_INITIALIZED();

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
    VS_CHECK_API_INITIALIZED();

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
    VS_CHECK_API_INITIALIZED();

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
    VS_CHECK_API_INITIALIZED();

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
    VS_CHECK_API_INITIALIZED();

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
    MUTEX();                                             \
    SWSS_LOG_ENTER();                                       \
    VS_CHECK_API_INITIALIZED();                                \
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
    MUTEX();                                             \
    SWSS_LOG_ENTER();                                       \
    VS_CHECK_API_INITIALIZED();                                \
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
    MUTEX();                                             \
    SWSS_LOG_ENTER();                                       \
    VS_CHECK_API_INITIALIZED();                                \
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
    VS_CHECK_API_INITIALIZED();

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
    VS_CHECK_API_INITIALIZED();

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
    VS_CHECK_API_INITIALIZED();

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

    if (!Globals::apiInitialized)
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

    if (!Globals::apiInitialized)
    {
        SWSS_LOG_ERROR("%s: SAI API not initialized", __PRETTY_FUNCTION__);

        return SAI_NULL_OBJECT_ID;
    }

    // not need for metadata check or mutex since this method is static

    return RealObjectIdManager::switchIdQuery(objectId);
}

