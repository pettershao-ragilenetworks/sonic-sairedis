#include "sai_redis.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

static sai_status_t redis_flush_fdb_entries(
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return g_meta->flushFdbEntries(
            switch_id,
            attr_count,
            attr_list,
            *g_remoteSaiInterface);
}

REDIS_GENERIC_QUAD_ENTRY(FDB_ENTRY,fdb_entry);

REDIS_BULK_CREATE_ENTRY(FDB_ENTRY,fdb_entry);
REDIS_BULK_REMOVE_ENTRY(FDB_ENTRY,fdb_entry);

// TODO remove when test corrected (SAI pointer must be advanced for this)

sai_status_t sai_bulk_create_fdb_entry(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t *fdb_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    return redis_bulk_create_fdb_entry(object_count, fdb_entry, attr_count, attr_list, mode, object_statuses);
}

sai_status_t sai_bulk_remove_fdb_entry(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t *fdb_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    return redis_bulk_remove_fdb_entry(object_count, fdb_entry, mode, object_statuses);
}

const sai_fdb_api_t redis_fdb_api = {

    REDIS_GENERIC_QUAD_API(fdb_entry)

    redis_flush_fdb_entries,
};
