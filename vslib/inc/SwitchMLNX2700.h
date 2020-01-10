#pragma once

#include "SwitchStateBase.h"

namespace saivs
{
    class SwitchMLNX2700:
        public SwitchStateBase
    {
        public:

            SwitchMLNX2700(
                    _In_ sai_object_id_t switch_id);

            virtual ~SwitchMLNX2700() = default;

        protected:

            virtual sai_status_t create_qos_queues_per_port(
                    _In_ sai_object_id_t switch_object_id,
                    _In_ sai_object_id_t port_id) override;

            virtual sai_status_t create_qos_queues() override;
    };
}

