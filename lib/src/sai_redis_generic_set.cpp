#include "sai_redis.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

sai_status_t internal_redis_generic_set(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &serialized_object_id,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> entry = SaiAttributeList::serialize_attr_list(
            object_type,
            1,
            attr,
            false);

    std::string str_object_type = sai_serialize_object_type(object_type);

    std::string key = str_object_type + ":" + serialized_object_id;

    SWSS_LOG_DEBUG("generic set key: %s, fields: %lu", key.c_str(), entry.size());

    if (g_record)
    {
        recordLine("s|" + key + "|" + joinFieldValues(entry));
    }

    g_asicState->set(key, entry, "set");

    return SAI_STATUS_SUCCESS;
}

sai_status_t internal_redis_bulk_generic_set(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ const sai_attribute_t *attr_list,
        _In_ const sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

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

        swss::FieldValueTuple fvtNoStatus(serialized_object_ids[idx] , str_attr);

        entries.push_back(fvtNoStatus);
    }

    /*
     * We are adding number of entries to actualy add ':' to be compatible
     * with previous
     */

    if (g_record)
    {
        std::string joined;

        for (const auto &e: entriesWithStatus)
        {
            // ||obj_id|attr=val|attr=val|status||obj_id|attr=val|attr=val|status

            joined += "||" + fvField(e) + "|" + fvValue(e);
        }

        /*
         * Capital 'S' stads for bulk SET operation.
         */

        recordLine("S|" + str_object_type + joined);
    }

    std::string key = str_object_type + ":" + std::to_string(entries.size());

    if (entries.size())
    {
        g_asicState->set(key, entries, "bulkset");
    }

    return SAI_STATUS_SUCCESS;
}


sai_status_t redis_generic_set(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    std::string str_object_id = sai_serialize_object_id(object_id);

    return internal_redis_generic_set(
            object_type,
            str_object_id,
            attr);
}

#define REDIS_ENTRY_SET(OT,ot)                          \
sai_status_t redis_generic_set_ ## ot(                  \
        _In_ const sai_ ## ot ## _t *entry,             \
        _In_ const sai_attribute_t *attr)               \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    std::string str = sai_serialize_ ## ot(*entry);     \
    return internal_redis_generic_set(                  \
            SAI_OBJECT_TYPE_ ## OT,                     \
            str,                                        \
            attr);                                      \
}

REDIS_ENTRY_SET(FDB_ENTRY,fdb_entry);
REDIS_ENTRY_SET(INSEG_ENTRY,inseg_entry);
REDIS_ENTRY_SET(IPMC_ENTRY,ipmc_entry);
REDIS_ENTRY_SET(L2MC_ENTRY,l2mc_entry);
REDIS_ENTRY_SET(MCAST_FDB_ENTRY,mcast_fdb_entry);
REDIS_ENTRY_SET(NEIGHBOR_ENTRY,neighbor_entry);
REDIS_ENTRY_SET(ROUTE_ENTRY,route_entry);
