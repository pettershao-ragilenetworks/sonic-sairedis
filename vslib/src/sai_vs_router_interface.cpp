#include "sai_vs.h"
#include "sai_vs_internal.h"

VS_GENERIC_QUAD(ROUTER_INTERFACE,router_interface);

sai_status_t vs_get_router_interface_stats(
        _In_ sai_object_id_t router_interface_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_router_interface_stat_t *counter_ids,
        _Out_ uint64_t *counters)
{
    MUTEX();

    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t vs_get_router_interface_stats_ext(
        _In_ sai_object_id_t router_interface_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_router_interface_stat_t *counter_ids,
        _In_ sai_stats_mode_t mode,
        _Out_ uint64_t *counters)
{
    MUTEX();

    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t vs_clear_router_interface_stats(
        _In_ sai_object_id_t router_interface_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_router_interface_stat_t *counter_ids)
{
    MUTEX();

    SWSS_LOG_ENTER();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

const sai_router_interface_api_t vs_router_interface_api = {

    VS_GENERIC_QUAD_API(router_interface)

    vs_get_router_interface_stats,
    vs_get_router_interface_stats_ext,
    vs_clear_router_interface_stats,
};
