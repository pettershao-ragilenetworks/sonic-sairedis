#include "sai_vs.h"

VS_GENERIC_QUAD(SWITCH,switch);
VS_GENERIC_STATS(SWITCH,switch);

static sai_status_t vs_create_switch_uniq(
        _Out_ sai_object_id_t *switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return vs_create_switch(
            switch_id,
            SAI_NULL_OBJECT_ID, // no switch id since we create switch
            attr_count,
            attr_list);
}

const sai_switch_api_t vs_switch_api = {

    vs_create_switch_uniq,
    vs_remove_switch,
    vs_set_switch_attribute,
    vs_get_switch_attribute,

    VS_GENERIC_STATS_API(switch)
};
