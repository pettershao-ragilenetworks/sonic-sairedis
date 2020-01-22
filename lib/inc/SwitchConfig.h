#pragma once

#include <string>
#include <memory>

namespace sairedis
{
    class SwitchConfig
    {
        public:

            SwitchConfig();

            virtual ~SwitchConfig() = default;

        public:

            uint32_t m_switchIndex;

            std::string m_hardwareInfo;
    };
}
