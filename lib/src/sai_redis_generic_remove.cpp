#include "sai_redis.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

sai_status_t internal_redis_generic_remove(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &serialized_object_id)
{
    SWSS_LOG_ENTER();

    std::string str_object_type = sai_serialize_object_type(object_type);

    std::string key = str_object_type + ":" + serialized_object_id;

    SWSS_LOG_DEBUG("generic remove key: %s", key.c_str());

    g_recorder->recordGenericRemove(key);

    g_asicState->del(key, "remove");

    auto status = internal_api_wait_for_response(SAI_COMMON_API_REMOVE);

    g_recorder->recordGenericRemoveResponse(status);

    return status;
}

sai_status_t redis_generic_remove(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id)
{
    SWSS_LOG_ENTER();

    std::string str_object_id = sai_serialize_object_id(object_id);

    sai_status_t status = internal_redis_generic_remove(
            object_type,
            str_object_id);

    if (object_type == SAI_OBJECT_TYPE_SWITCH &&
            status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_NOTICE("removing switch id %s", sai_serialize_object_id(object_id).c_str());

        g_virtualObjectIdManager->releaseObjectId(object_id);

        // TODO do we need some more actions here ? to clean all
        // objects that are in the same switch that were snooped
        // inside metadata ? should that be metadata job?
    }

    return status;
}

sai_status_t redis_bulk_generic_remove(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t object_count,
        _In_ const sai_object_id_t *object_id, /* array */
        _Out_ sai_status_t *object_statuses) /* array */
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_object_id(object_id[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return internal_redis_bulk_generic_remove(
            object_type,
            serialized_object_ids,
            object_statuses);
}

sai_status_t internal_redis_bulk_generic_remove(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _Out_ sai_status_t *object_statuses) /* array */
{
    SWSS_LOG_ENTER();

    std::string str_object_type = sai_serialize_object_type(object_type);

    std::vector<swss::FieldValueTuple> entries;

    for (size_t idx = 0; idx < serialized_object_ids.size(); ++idx)
    {
        std::string str_attr = "";

        swss::FieldValueTuple fvtNoStatus(serialized_object_ids[idx], str_attr);

        entries.push_back(fvtNoStatus);
    }

    g_recorder->recordBulkGenericRemove(str_object_type, entries);

    /*
     * We are adding number of entries to actually add ':' to be compatible
     * with previous
     */

    // key:         object_type:count
    // field:       object_id
    // value:       object_attrs
    std::string key = str_object_type + ":" + std::to_string(entries.size());

    if (entries.size())
    {
        g_asicState->set(key, entries, "bulkremove");
    }

    auto status = internal_api_wait_for_response(SAI_COMMON_API_REMOVE);

    uint32_t objectCount = (uint32_t)serialized_object_ids.size();

    g_recorder->recordBulkGenericRemoveResponse(status, objectCount, object_statuses);

    return status;
}


#define REDIS_ENTRY_REMOVE(OT,ot)                           \
    sai_status_t redis_generic_remove_ ## ot(               \
            _In_ const sai_ ## ot ## _t *entry)             \
        {                                                   \
            SWSS_LOG_ENTER();                               \
            std::string str = sai_serialize_ ## ot(*entry); \
            return internal_redis_generic_remove(           \
                    SAI_OBJECT_TYPE_ ## OT,                 \
                    str);                                   \
        }

REDIS_ENTRY_REMOVE(FDB_ENTRY,fdb_entry);
REDIS_ENTRY_REMOVE(INSEG_ENTRY,inseg_entry);
REDIS_ENTRY_REMOVE(IPMC_ENTRY,ipmc_entry);
REDIS_ENTRY_REMOVE(L2MC_ENTRY,l2mc_entry);
REDIS_ENTRY_REMOVE(MCAST_FDB_ENTRY,mcast_fdb_entry);
REDIS_ENTRY_REMOVE(NEIGHBOR_ENTRY,neighbor_entry);
REDIS_ENTRY_REMOVE(ROUTE_ENTRY,route_entry);
REDIS_ENTRY_REMOVE(NAT_ENTRY,nat_entry);
