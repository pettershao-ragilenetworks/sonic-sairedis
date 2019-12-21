#include "sai_redis.h"
#include "sairedis.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

REDIS_GENERIC_QUAD_ENTRY(ROUTE_ENTRY,route_entry);

REDIS_BULK_QUAD_ENTRY(ROUTE_ENTRY,route_entry);

// TODO remove when correcting tests
sai_status_t sai_bulk_remove_route_entry(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t *route_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    return redis_bulk_remove_route_entry(object_count, route_entry, mode, object_statuses);
}

sai_status_t sai_bulk_set_route_entry_attribute(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t *route_entry,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    return redis_bulk_set_route_entry(object_count, route_entry, attr_list, mode, object_statuses);
}

sai_status_t sai_bulk_create_route_entry(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t *route_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    return redis_bulk_create_route_entry(object_count, route_entry, attr_count, attr_list, mode, object_statuses);
}

const sai_route_api_t redis_route_api = {

    REDIS_GENERIC_QUAD_API(route_entry)
    REDIS_BULK_QUAD_API(route_entry)
};
