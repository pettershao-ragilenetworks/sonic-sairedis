#include "sai_redis.h"
#include "meta/sai_serialize.h"

volatile bool g_recordStats = true;

/*
 * Max number of counters used in 1 api call
 */
#define REDIS_MAX_COUNTERS 128

#define REDIS_COUNTERS_COUNT_MSB (0x80000000)


std::vector<swss::FieldValueTuple> serialize_counter_id_list(
        _In_ const sai_enum_metadata_t *stats_enum,
        _In_ uint32_t count,
        _In_ const sai_stat_id_t*counter_id_list)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> values;

    for (uint32_t i = 0; i < count; i++)
    {
        const char *name = sai_metadata_get_enum_value_name(stats_enum, counter_id_list[i]);

        if (name == NULL)
        {
            SWSS_LOG_THROW("failed to find enum %d in %s", counter_id_list[i], stats_enum->name);
        }

        values.emplace_back(name, "");
    }

    return values;
}
