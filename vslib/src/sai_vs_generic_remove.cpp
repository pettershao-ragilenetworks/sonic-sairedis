#include "sai_vs.h"
#include "sai_vs_state.h"


sai_status_t vs_generic_remove(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id)
{
    SWSS_LOG_ENTER();

    return g_vs->remove(object_type, object_id);
}

#define VS_ENTRY_REMOVE(OT,ot)                              \
    sai_status_t vs_generic_remove_ ## ot(                  \
            _In_ const sai_ ## ot ## _t *entry)             \
        {                                                   \
            SWSS_LOG_ENTER();                               \
            return g_vs->remove(entry);                     \
        }

VS_ENTRY_REMOVE(FDB_ENTRY,fdb_entry);
VS_ENTRY_REMOVE(INSEG_ENTRY,inseg_entry);
VS_ENTRY_REMOVE(IPMC_ENTRY,ipmc_entry);
VS_ENTRY_REMOVE(L2MC_ENTRY,l2mc_entry);
VS_ENTRY_REMOVE(MCAST_FDB_ENTRY,mcast_fdb_entry);
VS_ENTRY_REMOVE(NEIGHBOR_ENTRY,neighbor_entry);
VS_ENTRY_REMOVE(ROUTE_ENTRY,route_entry);
VS_ENTRY_REMOVE(NAT_ENTRY, nat_entry);
