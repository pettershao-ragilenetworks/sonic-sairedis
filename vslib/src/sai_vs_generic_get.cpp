#include "sai_vs.h"
#include "sai_vs_switch_BCM56850.h"
#include "sai_vs_switch_MLNX2700.h"

sai_status_t vs_generic_get(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t attr_count,
        _Out_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return g_vs->get(
            object_type,
            object_id,
            attr_count,
            attr_list);
}

#define VS_ENTRY_GET(OT,ot)                             \
sai_status_t vs_generic_get_ ## ot(                     \
        _In_ const sai_ ## ot ## _t *entry,             \
        _In_ uint32_t attr_count,                       \
        _Out_ sai_attribute_t *attr_list)               \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_vs->get(                                   \
            entry,                                      \
            attr_count,                                 \
            attr_list);                                 \
}

VS_ENTRY_GET(FDB_ENTRY,fdb_entry);
VS_ENTRY_GET(INSEG_ENTRY,inseg_entry);
VS_ENTRY_GET(IPMC_ENTRY,ipmc_entry);
VS_ENTRY_GET(L2MC_ENTRY,l2mc_entry);
VS_ENTRY_GET(MCAST_FDB_ENTRY,mcast_fdb_entry);
VS_ENTRY_GET(NEIGHBOR_ENTRY,neighbor_entry);
VS_ENTRY_GET(ROUTE_ENTRY,route_entry);
VS_ENTRY_GET(NAT_ENTRY, nat_entry);
