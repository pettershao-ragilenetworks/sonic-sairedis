#include "sai_vs.h"
#include "sai_vs_state.h"

sai_status_t vs_generic_set(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    return g_vs->set(object_type, object_id, attr);
}

#define VS_ENTRY_SET(OT,ot)                             \
sai_status_t vs_generic_set_ ## ot(                     \
        _In_ const sai_ ## ot ## _t *entry,             \
        _In_ const sai_attribute_t *attr)               \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_vs->set(entry, attr);                      \
}

VS_ENTRY_SET(FDB_ENTRY,fdb_entry);
VS_ENTRY_SET(INSEG_ENTRY,inseg_entry);
VS_ENTRY_SET(IPMC_ENTRY,ipmc_entry);
VS_ENTRY_SET(L2MC_ENTRY,l2mc_entry);
VS_ENTRY_SET(MCAST_FDB_ENTRY,mcast_fdb_entry);
VS_ENTRY_SET(NEIGHBOR_ENTRY,neighbor_entry);
VS_ENTRY_SET(ROUTE_ENTRY,route_entry);
VS_ENTRY_SET(NAT_ENTRY, nat_entry);
