#ifndef __SAI_VS__
#define __SAI_VS__

extern "C" {
#include "sai.h"
#include "saiextensions.h"
}

#include "meta/sai_meta.h"
#include "meta/sai_serialize.h"
#include "meta/Meta.h"

#include "Globals.h"
#include "RealObjectIdManager.h"
#include "VirtualSwitchSaiInterface.h"
#include "FdbInfo.h"
#include "SwitchState.h"
#include "LaneMapContainer.h"

#include "swss/logger.h"
#include "swss/tokenize.h"

#include <mutex>

#define SAI_VS_MAX_PORTS 1024

#define SAI_VS_VETH_PREFIX   "v"

typedef enum _sai_vs_switch_type_t
{
    SAI_VS_SWITCH_TYPE_NONE,

    SAI_VS_SWITCH_TYPE_BCM56850,

    SAI_VS_SWITCH_TYPE_MLNX2700,

} sai_vs_switch_type_t;

typedef enum _sai_vs_boot_type_t
{
    SAI_VS_BOOT_TYPE_COLD,

    SAI_VS_BOOT_TYPE_WARM,

    SAI_VS_BOOT_TYPE_FAST,

} sai_vs_boot_type_t;



extern bool                             g_vs_hostif_use_tap_device;
extern sai_vs_switch_type_t             g_vs_switch_type;

extern sai_vs_boot_type_t g_vs_boot_type;

extern const char *g_warm_boot_read_file;
extern const char *g_warm_boot_write_file;

extern std::shared_ptr<saivs::LaneMapContainer> g_laneMapContainer;

extern std::shared_ptr<saivs::RealObjectIdManager>       g_realObjectIdManager;
extern std::shared_ptr<saivs::VirtualSwitchSaiInterface> g_vs;
extern std::shared_ptr<saimeta::Meta>                    g_meta;

#define CHECK_STATUS(status)            \
    {                                   \
        sai_status_t s = (status);      \
        if (s != SAI_STATUS_SUCCESS)    \
            { return s; }               \
    }

#define DEFAULT_VLAN_NUMBER 1

#define SAI_VS_FDB_INFO "SAI_VS_FDB_INFO"

extern saivs::SwitchState::SwitchStateMap g_switch_state_map;

sai_status_t vs_recreate_hostif_tap_interfaces(
        _In_ sai_object_id_t switch_id);

void processFdbInfo(
        _In_ const saivs::FdbInfo& fi,
        _In_ sai_fdb_event_t fdb_event);

void update_port_oper_status(
        _In_ sai_object_id_t port_id,
        _In_ sai_port_oper_status_t port_oper_status);

std::shared_ptr<saivs::SwitchState> vs_get_switch_state(
        _In_ sai_object_id_t switch_id);

#define PRIVATE __attribute__((visibility("hidden")))

PRIVATE extern const sai_acl_api_t              vs_acl_api;
PRIVATE extern const sai_bfd_api_t              vs_bfd_api;
PRIVATE extern const sai_bmtor_api_t            vs_bmtor_api;
PRIVATE extern const sai_bridge_api_t           vs_bridge_api;
PRIVATE extern const sai_buffer_api_t           vs_buffer_api;
PRIVATE extern const sai_counter_api_t          vs_counter_api;
PRIVATE extern const sai_debug_counter_api_t    vs_debug_counter_api;
PRIVATE extern const sai_dtel_api_t             vs_dtel_api;
PRIVATE extern const sai_fdb_api_t              vs_fdb_api;
PRIVATE extern const sai_hash_api_t             vs_hash_api;
PRIVATE extern const sai_hostif_api_t           vs_hostif_api;
PRIVATE extern const sai_ipmc_api_t             vs_ipmc_api;
PRIVATE extern const sai_ipmc_group_api_t       vs_ipmc_group_api;
PRIVATE extern const sai_isolation_group_api_t  vs_isolation_group_api;
PRIVATE extern const sai_l2mc_api_t             vs_l2mc_api;
PRIVATE extern const sai_l2mc_group_api_t       vs_l2mc_group_api;
PRIVATE extern const sai_lag_api_t              vs_lag_api;
PRIVATE extern const sai_mcast_fdb_api_t        vs_mcast_fdb_api;
PRIVATE extern const sai_mirror_api_t           vs_mirror_api;
PRIVATE extern const sai_mpls_api_t             vs_mpls_api;
PRIVATE extern const sai_nat_api_t              vs_nat_api;
PRIVATE extern const sai_neighbor_api_t         vs_neighbor_api;
PRIVATE extern const sai_next_hop_api_t         vs_next_hop_api;
PRIVATE extern const sai_next_hop_group_api_t   vs_next_hop_group_api;
PRIVATE extern const sai_policer_api_t          vs_policer_api;
PRIVATE extern const sai_port_api_t             vs_port_api;
PRIVATE extern const sai_qos_map_api_t          vs_qos_map_api;
PRIVATE extern const sai_queue_api_t            vs_queue_api;
PRIVATE extern const sai_route_api_t            vs_route_api;
PRIVATE extern const sai_router_interface_api_t vs_router_interface_api;
PRIVATE extern const sai_rpf_group_api_t        vs_rpf_group_api;
PRIVATE extern const sai_samplepacket_api_t     vs_samplepacket_api;
PRIVATE extern const sai_scheduler_api_t        vs_scheduler_api;
PRIVATE extern const sai_scheduler_group_api_t  vs_scheduler_group_api;
PRIVATE extern const sai_segmentroute_api_t     vs_segmentroute_api;
PRIVATE extern const sai_stp_api_t              vs_stp_api;
PRIVATE extern const sai_switch_api_t           vs_switch_api;
PRIVATE extern const sai_tam_api_t              vs_tam_api;
PRIVATE extern const sai_tunnel_api_t           vs_tunnel_api;
PRIVATE extern const sai_udf_api_t              vs_udf_api;
PRIVATE extern const sai_virtual_router_api_t   vs_virtual_router_api;
PRIVATE extern const sai_vlan_api_t             vs_vlan_api;
PRIVATE extern const sai_wred_api_t             vs_wred_api;

// OID QUAD

sai_status_t vs_generic_create(
        _In_ sai_object_type_t object_type,
        _Out_ sai_object_id_t *object_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list);

sai_status_t vs_generic_remove(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id);

sai_status_t vs_generic_set(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_attribute_t *attr);

sai_status_t vs_generic_get(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t attr_count,
        _Out_ sai_attribute_t *attr_list);

// ENTRY QUAD

#define VS_CREATE_ENTRY_DEF(ot)                     \
    sai_status_t vs_generic_create_ ## ot(          \
            _In_ const sai_ ## ot ## _t * ot,       \
            _In_ uint32_t attr_count,               \
            _In_ const sai_attribute_t *attr_list);

#define VS_REMOVE_ENTRY_DEF(ot)                     \
    sai_status_t vs_generic_remove_ ## ot(          \
            _In_ const sai_ ## ot ## _t * ot);

#define VS_SET_ENTRY_DEF(ot)                        \
    sai_status_t vs_generic_set_ ## ot(             \
            _In_ const sai_ ## ot ## _t * ot,       \
            _In_ const sai_attribute_t *attr);

#define VS_GET_ENTRY_DEF(ot)                        \
    sai_status_t vs_generic_get_ ## ot(             \
            _In_ const sai_ ## ot ## _t * ot,       \
            _In_ uint32_t attr_count,               \
            _Out_ sai_attribute_t *attr_list);

#define VS_ENTRY_QUAD(ot)       \
    VS_CREATE_ENTRY_DEF(ot)     \
    VS_REMOVE_ENTRY_DEF(ot)     \
    VS_SET_ENTRY_DEF(ot)        \
    VS_GET_ENTRY_DEF(ot)

VS_ENTRY_QUAD(fdb_entry);
VS_ENTRY_QUAD(inseg_entry);
VS_ENTRY_QUAD(ipmc_entry);
VS_ENTRY_QUAD(l2mc_entry);
VS_ENTRY_QUAD(mcast_fdb_entry);
VS_ENTRY_QUAD(neighbor_entry);
VS_ENTRY_QUAD(route_entry);
VS_ENTRY_QUAD(nat_entry);

#endif // __SAI_VS__
