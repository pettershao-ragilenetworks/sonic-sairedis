#include "sai_redis.h"

REDIS_GENERIC_QUAD(SWITCH,switch);
REDIS_GENERIC_STATS(SWITCH,switch);

static sai_status_t redis_create_switch_uniq(
        _Out_ sai_object_id_t *switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return redis_create_switch(
            switch_id,
            SAI_NULL_OBJECT_ID, // no switch id since we create switch
            attr_count,
            attr_list);
}

const sai_switch_api_t redis_switch_api = {

    redis_create_switch_uniq,
    redis_remove_switch,
    redis_set_switch_attribute,
    redis_get_switch_attribute,

    REDIS_GENERIC_STATS_API(switch)
};
