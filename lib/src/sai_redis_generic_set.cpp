#include "sai_redis.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

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

        swss::FieldValueTuple fvtNoStatus(serialized_object_ids[idx] , str_attr);

        entries.push_back(fvtNoStatus);
    }

    g_recorder->recordBulkGenericSet(str_object_type, entries);

    /*
     * We are adding number of entries to actually add ':' to be compatible
     * with previous
     */

    std::string key = str_object_type + ":" + std::to_string(entries.size());

    if (entries.size())
    {
        g_asicState->set(key, entries, "bulkset");
    }

    auto status = internal_api_wait_for_response(SAI_COMMON_API_BULK_SET);

    uint32_t objectCount = (uint32_t)serialized_object_ids.size();

    g_recorder->recordBulkGenericSetResponse(status, objectCount, object_statuses);

    return status;
}

