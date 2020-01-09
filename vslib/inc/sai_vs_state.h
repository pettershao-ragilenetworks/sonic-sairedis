#ifndef __SAI_VS_STATE__
#define __SAI_VS_STATE__

#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

#include "swss/selectableevent.h"

#include "RealObjectIdManager.h"
#include "FdbInfo.h"
#include "SaiAttrWrap.h"
#include "SwitchState.h"

#include <unordered_map>
#include <string>
#include <set>

#define CHECK_STATUS(status)            \
    {                                   \
        sai_status_t s = (status);      \
        if (s != SAI_STATUS_SUCCESS)    \
            { return s; }               \
    }

#define DEFAULT_VLAN_NUMBER 1

#define SAI_VS_FDB_INFO "SAI_VS_FDB_INFO"

extern std::set<saivs::FdbInfo> g_fdb_info_set;

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

#endif // __SAI_VS_STATE__
