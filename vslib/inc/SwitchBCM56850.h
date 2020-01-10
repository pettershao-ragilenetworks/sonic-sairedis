#pragma once

#include "SwitchStateBase.h"

namespace saivs
{
    class SwitchBCM56850:
        public SwitchStateBase
    {
        public:

            SwitchBCM56850(
                    _In_ sai_object_id_t switch_id);

            virtual ~SwitchBCM56850() = default;

        protected:

            virtual sai_status_t create_qos_queues_per_port(
                    _In_ sai_object_id_t switch_object_id,
                    _In_ sai_object_id_t port_id) override;

            virtual sai_status_t create_qos_queues() override;
    };
}
