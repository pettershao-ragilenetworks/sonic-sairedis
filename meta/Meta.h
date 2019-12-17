#pragma once

#include "lib/inc/SaiInterface.h"

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
    };
};
