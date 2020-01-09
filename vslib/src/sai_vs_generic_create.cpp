#include "sai_vs.h"
#include "sai_vs_state.h"
#include "sai_vs_switch_BCM56850.h"
#include "sai_vs_switch_MLNX2700.h"

sai_status_t vs_generic_create(
        _In_ sai_object_type_t object_type,
        _Out_ sai_object_id_t *object_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return g_vs->create(
            object_type,
            object_id,
            switch_id,
            attr_count,
            attr_list);
}

#define VS_ENTRY_CREATE(OT,ot)                          \
    sai_status_t vs_generic_create_ ## ot(              \
            _In_ const sai_ ## ot ## _t * entry,        \
            _In_ uint32_t attr_count,                   \
            _In_ const sai_attribute_t *attr_list)      \
    {                                                   \
        SWSS_LOG_ENTER();                               \
        return g_vs->create(                            \
                entry,                                  \
                attr_count,                             \
                attr_list);                             \
    }

VS_ENTRY_CREATE(FDB_ENTRY,fdb_entry);
VS_ENTRY_CREATE(INSEG_ENTRY,inseg_entry);
VS_ENTRY_CREATE(IPMC_ENTRY,ipmc_entry);
VS_ENTRY_CREATE(L2MC_ENTRY,l2mc_entry);
VS_ENTRY_CREATE(MCAST_FDB_ENTRY,mcast_fdb_entry);
VS_ENTRY_CREATE(NEIGHBOR_ENTRY,neighbor_entry);
VS_ENTRY_CREATE(ROUTE_ENTRY,route_entry);
VS_ENTRY_CREATE(NAT_ENTRY, nat_entry);
