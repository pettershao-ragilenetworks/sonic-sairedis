#pragma once

extern "C" {
#include "sai.h"
#include "saimetadata.h"
}

namespace sairedis
{
    class SaiInterface
    {
        public:

            SaiInterface() = default;

            virtual ~SaiInterface() = default;

        public: // remove

            virtual sai_status_t remove(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId) = 0;

    };
}
