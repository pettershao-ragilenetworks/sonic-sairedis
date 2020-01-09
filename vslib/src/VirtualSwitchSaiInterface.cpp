#include "VirtualSwitchSaiInterface.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

#include <inttypes.h>

#include "sai_vs.h"
#include "sai_vs_state.h"
#include "meta/sai_serialize.h"
#include "sai_vs_switch_BCM56850.h"
#include "sai_vs_switch_MLNX2700.h"

/*
 * Max number of counters used in 1 api call
 */
#define VS_MAX_COUNTERS 128

#define VS_COUNTERS_COUNT_MSB (0x80000000)

using namespace saivs;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

VirtualSwitchSaiInterface::VirtualSwitchSaiInterface()
{
    SWSS_LOG_ENTER();

    // empty
}

VirtualSwitchSaiInterface::~VirtualSwitchSaiInterface()
{
    SWSS_LOG_ENTER();

    // empty
}

sai_status_t VirtualSwitchSaiInterface::create(
        _In_ sai_object_type_t objectType,
        _Out_ sai_object_id_t* objectId,
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    if (!objectId || *objectId == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_THROW("forgot to allocate object id, FATAL");
    }

    // NOTE: objectId was allocated by the caller

    return create(
            objectType,
            sai_serialize_object_id(*objectId),
            attr_count,
            attr_list);
}

sai_status_t VirtualSwitchSaiInterface::remove(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return remove(
            objectType,
            sai_serialize_object_id(objectId));
}

sai_status_t VirtualSwitchSaiInterface::set(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    return set(
            objectType,
            sai_serialize_object_id(objectId),
            attr);
}

sai_status_t VirtualSwitchSaiInterface::get(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return get(
            objectType,
            sai_serialize_object_id(objectId),
            attr_count,
            attr_list);
}


#define DECLARE_REMOVE_ENTRY(OT,ot)                             \
sai_status_t VirtualSwitchSaiInterface::remove(                 \
        _In_ const sai_ ## ot ## _t* ot)                        \
{                                                               \
    SWSS_LOG_ENTER();                                           \
    return remove(                                              \
            SAI_OBJECT_TYPE_ ## OT,                             \
            sai_serialize_ ## ot(*ot));                         \
}

DECLARE_REMOVE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_REMOVE_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_REMOVE_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_REMOVE_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_REMOVE_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_REMOVE_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_REMOVE_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_REMOVE_ENTRY(NAT_ENTRY,nat_entry);

#define DECLARE_CREATE_ENTRY(OT,ot)                             \
sai_status_t VirtualSwitchSaiInterface::create(                 \
        _In_ const sai_ ## ot ## _t* ot,                        \
        _In_ uint32_t attr_count,                               \
        _In_ const sai_attribute_t *attr_list)                  \
{                                                               \
    SWSS_LOG_ENTER();                                           \
    return create(                                              \
            SAI_OBJECT_TYPE_ ## OT,                             \
            sai_serialize_ ## ot(*ot),                          \
            attr_count,                                         \
            attr_list);                                         \
}

DECLARE_CREATE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_CREATE_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_CREATE_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_CREATE_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_CREATE_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_CREATE_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_CREATE_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_CREATE_ENTRY(NAT_ENTRY,nat_entry);

#define DECLARE_SET_ENTRY(OT,ot)                                \
sai_status_t VirtualSwitchSaiInterface::set(                    \
        _In_ const sai_ ## ot ## _t* ot,                        \
        _In_ const sai_attribute_t *attr)                       \
{                                                               \
    SWSS_LOG_ENTER();                                           \
    return set(                                                 \
            SAI_OBJECT_TYPE_ ## OT,                             \
            sai_serialize_ ## ot(*ot),                          \
            attr);                                              \
}

DECLARE_SET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_SET_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_SET_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_SET_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_SET_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_SET_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_SET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_SET_ENTRY(NAT_ENTRY,nat_entry);

sai_status_t VirtualSwitchSaiInterface::create(
        _In_ sai_object_type_t object_type,
        _In_ const std::string& serializedObjectId,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    throw; // TODO
}

static sai_status_t internal_vs_generic_remove(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &serialized_object_id,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    auto &objectHash = g_switch_state_map.at(switch_id)->objectHash.at(object_type);

    auto it = objectHash.find(serialized_object_id);

    if (it == objectHash.end())
    {
        SWSS_LOG_ERROR("not found %s:%s",
                sai_serialize_object_type(object_type).c_str(),
                serialized_object_id.c_str());

        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    objectHash.erase(it);

    return SAI_STATUS_SUCCESS;
}

static void vs_dump_switch_database_for_warm_restart(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    auto it = g_switch_state_map.find(switch_id);

    if (it == g_switch_state_map.end())
    {
        SWSS_LOG_THROW("switch don't exists 0x%" PRIx64, switch_id);
    }

    if (g_warm_boot_write_file == NULL)
    {
        SWSS_LOG_ERROR("warm boot write file is NULL");
        return;
    }

    std::ofstream dumpFile;

    dumpFile.open(g_warm_boot_write_file);

    if (!dumpFile.is_open())
    {
        SWSS_LOG_ERROR("failed to open: %s", g_warm_boot_write_file);
        return;
    }

    auto switchState = it->second;

    auto objectHash = switchState->objectHash;

    // dump all objects and attributes to file

    size_t count = 0;

    for (auto kvp: objectHash)
    {
        auto singleTypeObjectMap = kvp.second;

        count += singleTypeObjectMap.size();

        for (auto o: singleTypeObjectMap)
        {
            // if object don't have attributes, size can be zero
            if (o.second.size() == 0)
            {
                dumpFile << sai_serialize_object_type(kvp.first) << " " << o.first << " NULL NULL" << std::endl;
            }
            else
            {
                for (auto a: o.second)
                {
                    dumpFile << sai_serialize_object_type(kvp.first) << " ";
                    dumpFile << o.first.c_str();
                    dumpFile << " ";
                    dumpFile << a.first.c_str();
                    dumpFile << " ";
                    dumpFile << a.second->getAttrStrValue();
                    dumpFile << std::endl;
                }
            }
        }
    }

    if (g_vs_hostif_use_tap_device)
    {
        /*
         * If user is using tap devices we also need to dump local fdb info
         * data and restore it on warm start.
         */

        for (auto fi: g_fdb_info_set)
        {
            dumpFile << SAI_VS_FDB_INFO << " " << sai_vs_serialize_fdb_info(fi) << std::endl;
        }

        SWSS_LOG_NOTICE("dumped %zu fdb infos", g_fdb_info_set.size());
    }

    dumpFile.close();

    SWSS_LOG_NOTICE("dumped %zu objects to %s", count, g_warm_boot_write_file);
}

sai_status_t VirtualSwitchSaiInterface::remove(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& serializedObjectId)
{
    SWSS_LOG_ENTER();

    // std::string str_object_id = sai_serialize_object_id(object_id);
    // sai_object_id_t switch_id = g_realObjectIdManager->saiSwitchIdQuery(object_id);

    sai_object_id_t switch_id;

    if (g_switch_state_map.size() == 1)
    {
        switch_id = g_switch_state_map.begin()->first;
    }
    else
    {
        SWSS_LOG_THROW("multiple switches not supported, FIXME");
    }

    /*
     * Perform db dump if warm restart was requested.
     */

    if (objectType == SAI_OBJECT_TYPE_SWITCH)
    {
        sai_attribute_t attr;

        attr.id = SAI_SWITCH_ATTR_RESTART_WARM;

        sai_object_id_t object_id;
        sai_deserialize_object_id(serializedObjectId, object_id);

        if (vs_generic_get(objectType, object_id, 1, &attr) == SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_NOTICE("SAI_SWITCH_ATTR_RESTART_WARM = %s", attr.value.booldata ? "true" : "false");

            if (attr.value.booldata)
            {
                // TODO should be done on uninitialize and contain all switches
                // that has this flag true on multiple switches scenario
                vs_dump_switch_database_for_warm_restart(object_id);
            }
        }
        else
        {
            SWSS_LOG_ERROR("failed to get SAI_SWITCH_ATTR_RESTART_WARM, no DB dump will be performed");
        }
    }

    sai_status_t status = internal_vs_generic_remove(
            objectType,
            serializedObjectId,
            switch_id);

    if (objectType == SAI_OBJECT_TYPE_SWITCH &&
            status == SAI_STATUS_SUCCESS)
    {
        sai_object_id_t object_id;
        sai_deserialize_object_id(serializedObjectId, object_id);

        SWSS_LOG_NOTICE("removed switch: %s", sai_serialize_object_id(object_id).c_str());

        g_realObjectIdManager->releaseObjectId(object_id);

        switch (g_vs_switch_type)
        {
            case SAI_VS_SWITCH_TYPE_BCM56850:
                uninit_switch_BCM56850(object_id);
                break;

            case SAI_VS_SWITCH_TYPE_MLNX2700:
                uninit_switch_MLNX2700(object_id);
                break;

            default:
                SWSS_LOG_WARN("unknown switch type: %d", g_vs_switch_type);
                break;
        }
    }

    return status;
}

static sai_status_t internal_vs_generic_set(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &serialized_object_id,
        _In_ sai_object_id_t switch_id,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    if (g_switch_state_map.size() == 1)
    {
        switch_id = g_switch_state_map.begin()->first;
    }
    else
    {
        SWSS_LOG_THROW("multiple switches not supported, FIXME");
    }

    auto &objectHash = g_switch_state_map.at(switch_id)->objectHash.at(object_type);

    auto it = objectHash.find(serialized_object_id);

    if (it == objectHash.end())
    {
        SWSS_LOG_ERROR("not found %s:%s",
                sai_serialize_object_type(object_type).c_str(),
                serialized_object_id.c_str());

        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    AttrHash &attrHash = it->second;

    auto a = std::make_shared<SaiAttrWrap>(object_type, attr);

    // set have only one attribute
    attrHash[a->getAttrMetadata()->attridname] = a;

    return SAI_STATUS_SUCCESS;
}

sai_status_t VirtualSwitchSaiInterface::set(
        _In_ sai_object_type_t objectType,
        _In_ const std::string &serializedObjectId,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    // TODO we will need switch id when multiple switches will be supported or
    // each switch will have it's own interface, then not needed

    sai_object_id_t switch_id = SAI_NULL_OBJECT_ID; // sai_switch_id_query(object_id);

    return internal_vs_generic_set(
            objectType,
            serializedObjectId,
            switch_id,
            attr);
}

sai_status_t VirtualSwitchSaiInterface::get(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& serializedObjectId,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    throw; // TODO
}

#define DECLARE_GET_ENTRY(OT,ot)                                \
sai_status_t VirtualSwitchSaiInterface::get(                    \
        _In_ const sai_ ## ot ## _t* ot,                        \
        _In_ uint32_t attr_count,                               \
        _Inout_ sai_attribute_t *attr_list)                     \
{                                                               \
    SWSS_LOG_ENTER();                                           \
    return get(                                                 \
            SAI_OBJECT_TYPE_ ## OT,                             \
            sai_serialize_ ## ot(*ot),                          \
            attr_count,                                         \
            attr_list);                                         \
}

DECLARE_GET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_GET_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_GET_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_GET_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_GET_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_GET_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_GET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_GET_ENTRY(NAT_ENTRY,nat_entry);

sai_status_t VirtualSwitchSaiInterface::flushFdbEntries(
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList)
{
    SWSS_LOG_ENTER();

    throw; // TODO
}

sai_status_t VirtualSwitchSaiInterface::objectTypeGetAvailability(
        _In_ sai_object_id_t switchId,
        _In_ sai_object_type_t objectType,
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList,
        _Out_ uint64_t *count)
{
    SWSS_LOG_ENTER();

    // TODO: We should generate this metadata for the virtual switch rather
    // than hard-coding it here.
    //
    // TODO what about attribute list?

    if (objectType == SAI_OBJECT_TYPE_DEBUG_COUNTER)
    {
        *count = 3;
        return SAI_STATUS_SUCCESS;
    }

    return SAI_STATUS_NOT_SUPPORTED;
}

sai_status_t VirtualSwitchSaiInterface::queryAattributeEnumValuesCapability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ sai_attr_id_t attr_id,
        _Inout_ sai_s32_list_t *enum_values_capability)
{
    SWSS_LOG_ENTER();

    // TODO: We should generate this metadata for the virtual switch rather
    // than hard-coding it here.

    if (object_type == SAI_OBJECT_TYPE_DEBUG_COUNTER && attr_id == SAI_DEBUG_COUNTER_ATTR_IN_DROP_REASON_LIST)
    {
        if (enum_values_capability->count < 3)
        {
            return SAI_STATUS_BUFFER_OVERFLOW;
        }

        enum_values_capability->count = 3;
        enum_values_capability->list[0] = SAI_IN_DROP_REASON_L2_ANY;
        enum_values_capability->list[1] = SAI_IN_DROP_REASON_L3_ANY;
        enum_values_capability->list[2] = SAI_IN_DROP_REASON_ACL_ANY;

        return SAI_STATUS_SUCCESS;
    }
    else if (object_type == SAI_OBJECT_TYPE_DEBUG_COUNTER && attr_id == SAI_DEBUG_COUNTER_ATTR_OUT_DROP_REASON_LIST)
    {
        if (enum_values_capability->count < 2)
        {
            return SAI_STATUS_BUFFER_OVERFLOW;
        }

        enum_values_capability->count = 2;
        enum_values_capability->list[0] = SAI_OUT_DROP_REASON_L2_ANY;
        enum_values_capability->list[1] = SAI_OUT_DROP_REASON_L3_ANY;

        return SAI_STATUS_SUCCESS;
    }
        
    return SAI_STATUS_NOT_SUPPORTED;
}

sai_status_t VirtualSwitchSaiInterface::getStats(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids,
        _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    /*
     * Get stats is the same as get stats ext with mode == SAI_STATS_MODE_READ.
     */

    return getStatsExt(
            object_type,
            object_id,
            number_of_counters,
            counter_ids,
            SAI_STATS_MODE_READ,
            counters);
}

static sai_status_t internal_vs_generic_stats_function(
        _In_ sai_object_type_t obejct_type,
        _In_ sai_object_id_t object_id,
        _In_ sai_object_id_t switch_id,
        _In_ const sai_enum_metadata_t *enum_metadata,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t*counter_ids,
        _In_ sai_stats_mode_t mode,
        _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    bool perform_set = false;

    if (meta_unittests_enabled() && (number_of_counters & VS_COUNTERS_COUNT_MSB ))
    {
        number_of_counters &= ~VS_COUNTERS_COUNT_MSB;

        SWSS_LOG_NOTICE("unittests are enabled and counters count MSB is set to 1, performing SET on %s counters (%s)",
                sai_serialize_object_id(object_id).c_str(),
                enum_metadata->name);

        perform_set = true;
    }

    auto &countersMap = g_switch_state_map.at(switch_id)->countersMap;

    auto str_object_id = sai_serialize_object_id(object_id);

    auto mapit = countersMap.find(str_object_id);

    if (mapit == countersMap.end())
        countersMap[str_object_id] = std::map<int,uint64_t>();

    std::map<int,uint64_t>& localcounters = countersMap[str_object_id];

    for (uint32_t i = 0; i < number_of_counters; ++i)
    {
        int32_t id = counter_ids[i];

        if (perform_set)
        {
            localcounters[ id ] = counters[i];
        }
        else
        {
            auto it = localcounters.find(id);

            if (it == localcounters.end())
            {
                // if counter is not found on list, just return 0
                counters[i] = 0;
            }
            else
                counters[i] = it->second;

            if (mode == SAI_STATS_MODE_READ_AND_CLEAR)
            {
                localcounters[ id ] = 0;
            }
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t VirtualSwitchSaiInterface::getStatsExt(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids,
        _In_ sai_stats_mode_t mode,
        _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    /*
     * Do all parameter validation.
     *
     * TODO this should be done in metadata
     */

    if (object_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("object id is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t ot = g_realObjectIdManager->saiObjectTypeQuery(object_id);

    if (ot != object_type)
    {
        SWSS_LOG_ERROR("object %s is %s but expected %s",
                sai_serialize_object_id(object_id).c_str(),
                sai_serialize_object_type(ot).c_str(),
                sai_serialize_object_type(object_type).c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t switch_id = sai_switch_id_query(object_id);

    if (switch_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("object %s does not correspond to any switch object",
                sai_serialize_object_id(object_id).c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    uint32_t count = number_of_counters & ~VS_COUNTERS_COUNT_MSB;

    if (count > VS_MAX_COUNTERS)
    {
        SWSS_LOG_ERROR("max supported counters to get/clear is %u, but %u given",
                VS_MAX_COUNTERS,
                count);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (counter_ids == NULL)
    {
        SWSS_LOG_ERROR("counter ids pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (counters == NULL)
    {
        SWSS_LOG_ERROR("counters output pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    auto enum_metadata = sai_metadata_get_object_type_info(object_type)->statenum;

    if (enum_metadata == NULL)
    {
        SWSS_LOG_ERROR("enum metadata pointer is NULL, bug?");

        return SAI_STATUS_FAILURE;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        if (sai_metadata_get_enum_value_name(enum_metadata, counter_ids[i]) == NULL)
        {
            SWSS_LOG_ERROR("counter id %u is not allowed on %s", counter_ids[i], enum_metadata->name);

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    switch (mode)
    {
        case SAI_STATS_MODE_READ:
        case SAI_STATS_MODE_READ_AND_CLEAR:
            break;

        default:

            SWSS_LOG_ERROR("counters mode is invalid %d", mode);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    return internal_vs_generic_stats_function(
            object_type,
            object_id,
            switch_id,
            enum_metadata,
            number_of_counters,
            counter_ids,
            mode,
            counters);
}

sai_status_t VirtualSwitchSaiInterface::clearStats(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids)
{
    SWSS_LOG_ENTER();

    /*
     * Clear stats is the same as get stats ext with mode ==
     * SAI_STATS_MODE_READ_AND_CLEAR and we just read counters locally and
     * discard them, in that way.
     */

    uint64_t counters[VS_MAX_COUNTERS];

    return getStatsExt(
            object_type,
            object_id,
            number_of_counters,
            counter_ids,
            SAI_STATS_MODE_READ_AND_CLEAR,
            counters);
}

sai_status_t VirtualSwitchSaiInterface::bulkRemove(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO implement
    throw;
}

sai_status_t VirtualSwitchSaiInterface::bulkRemove(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t object_count,
        _In_ const sai_object_id_t *object_id,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_object_id(object_id[idx]));
    }

    return bulkRemove(object_type, serializedObjectIds, mode, object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkRemove(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t *route_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_route_entry(route_entry[idx]));
    }

    return bulkRemove(SAI_OBJECT_TYPE_ROUTE_ENTRY, serializedObjectIds, mode, object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkRemove(
        _In_ uint32_t object_count,
        _In_ const sai_nat_entry_t *nat_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_nat_entry(nat_entry[idx]));
    }

    return bulkRemove(SAI_OBJECT_TYPE_NAT_ENTRY, serializedObjectIds, mode, object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkRemove(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t *fdb_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_fdb_entry(fdb_entry[idx]));
    }

    return bulkRemove(SAI_OBJECT_TYPE_FDB_ENTRY, serializedObjectIds, mode, object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkSet(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t object_count,
        _In_ const sai_object_id_t *object_id,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_object_id(object_id[idx]));
    }

    return bulkSet(object_type, serializedObjectIds, attr_list, mode, object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkSet(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t *route_entry,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_route_entry(route_entry[idx]));
    }

    return bulkSet(SAI_OBJECT_TYPE_ROUTE_ENTRY, serializedObjectIds, attr_list, mode, object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkSet(
        _In_ uint32_t object_count,
        _In_ const sai_nat_entry_t *nat_entry,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_nat_entry(nat_entry[idx]));
    }

    return bulkSet(SAI_OBJECT_TYPE_NAT_ENTRY, serializedObjectIds, attr_list, mode, object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkSet(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t *fdb_entry,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_fdb_entry(fdb_entry[idx]));
    }

    return bulkSet(SAI_OBJECT_TYPE_FDB_ENTRY, serializedObjectIds, attr_list, mode, object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkSet(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    throw; // TODO
}

sai_status_t VirtualSwitchSaiInterface::bulkCreate(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t object_count,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_object_id_t *object_id,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_object_id(object_id[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return bulkCreate(
            object_type,
            serialized_object_ids,
            attr_count,
            attr_list,
            mode,
            object_statuses);
}

sai_status_t VirtualSwitchSaiInterface::bulkCreate(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Inout_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    throw; // TODO
}

sai_status_t VirtualSwitchSaiInterface::bulkCreate(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t* route_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_route_entry(route_entry[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return bulkCreate(
            SAI_OBJECT_TYPE_ROUTE_ENTRY,
            serialized_object_ids,
            attr_count,
            attr_list,
            mode,
            object_statuses);
}


sai_status_t VirtualSwitchSaiInterface::bulkCreate(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_fdb_entry(fdb_entry[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return bulkCreate(
            SAI_OBJECT_TYPE_FDB_ENTRY,
            serialized_object_ids,
            attr_count,
            attr_list,
            mode,
            object_statuses);
}


sai_status_t VirtualSwitchSaiInterface::bulkCreate(
        _In_ uint32_t object_count,
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_nat_entry(nat_entry[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return bulkCreate(
            SAI_OBJECT_TYPE_NAT_ENTRY,
            serialized_object_ids,
            attr_count,
            attr_list,
            mode,
            object_statuses);
}

sai_object_type_t VirtualSwitchSaiInterface::objectTypeQuery(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return g_realObjectIdManager->saiObjectTypeQuery(objectId);
}

sai_object_id_t VirtualSwitchSaiInterface::switchIdQuery(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return g_realObjectIdManager->saiSwitchIdQuery(objectId);
}

#pragma GCC diagnostic pop
