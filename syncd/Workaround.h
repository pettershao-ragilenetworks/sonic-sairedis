#pragma once

extern "C" {
#include "saimetadata.h"
}

namespace syncd
{
    class Workaround
    {
        private:

            Workaround() = delete;
            ~Workaround() = delete;

        public:

            /**
             * @brief Determines whether attribute is "workaround" attribute for SET API.
             *
             * Some attributes are not supported on SET API on different platforms.
             * For example SAI_SWITCH_ATTR_SRC_MAC_ADDRESS.
             *
             * @param[in] objecttype Object type.
             * @param[in] attrid Attribute Id.
             * @param[in] status Status from SET API.
             *
             * @return True if error from SET API can be ignored, false otherwise.
             */
            static bool isSetAttributeWorkaround(
                    _In_ sai_object_type_t objecttype,
                    _In_ sai_attr_id_t attrid,
                    _In_ sai_status_t status);
    };
}
