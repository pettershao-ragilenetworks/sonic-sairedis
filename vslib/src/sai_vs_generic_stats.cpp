#include "sai_vs.h"
#include "sai_vs_state.h"
#include "meta/sai_serialize.h"

sai_status_t vs_generic_get_stats(
    _In_ sai_object_type_t object_type,
    _In_ sai_object_id_t object_id,
    _In_ const sai_enum_metadata_t *enum_metadata,
    _In_ uint32_t number_of_counters,
    _In_ const sai_stat_id_t*counter_ids,
    _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    return g_vs->getStats(
            object_type,
            object_id,
            number_of_counters,
            counter_ids,
            counters);
}

sai_status_t vs_generic_get_stats_ext(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_enum_metadata_t *enum_metadata,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t*counter_ids,
        _In_ sai_stats_mode_t mode,
        _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    return g_vs->getStatsExt(
            object_type,
            object_id,
            number_of_counters,
            counter_ids,
            mode,
            counters);
}

sai_status_t vs_generic_clear_stats(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_enum_metadata_t *enum_metadata,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t*counter_ids)
{
    SWSS_LOG_ENTER();

    return g_vs->clearStats(
            object_type,
            object_id,
            number_of_counters,
            counter_ids);
}
