#pragma once

#include "lib/inc/SaiInterface.h"

#define SAIMETA_META_DECLARE_REMOVE_ENTRY(ot)                   \
    sai_status_t remove(                                        \
            _In_ const sai_ ## ot ## _t* ot,                    \
            _Inout_ sairedis::SaiInterface& saiInterface);

namespace saimeta
{
    class Meta
    {
        public:

            Meta() = default;

            virtual ~Meta() = default;

        public:

            sai_status_t remove(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _Inout_ sairedis::SaiInterface& saiInterface);

            SAIMETA_META_DECLARE_REMOVE_ENTRY(fdb_entry);
            SAIMETA_META_DECLARE_REMOVE_ENTRY(inseg_entry);
            SAIMETA_META_DECLARE_REMOVE_ENTRY(ipmc_entry);
            SAIMETA_META_DECLARE_REMOVE_ENTRY(l2mc_entry);
            SAIMETA_META_DECLARE_REMOVE_ENTRY(mcast_fdb_entry);
            SAIMETA_META_DECLARE_REMOVE_ENTRY(neighbor_entry);
            SAIMETA_META_DECLARE_REMOVE_ENTRY(route_entry);
            SAIMETA_META_DECLARE_REMOVE_ENTRY(nat_entry);

    };
};
