#pragma once

#include "LaneMap.h"
#include "EventQueue.h"

#include <string>
#include <memory>

namespace saivs
{
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

    class SwitchConfig
    {
        public:

            SwitchConfig();

            virtual ~SwitchConfig() = default;

        public:

            static bool parseSwitchType(
                    _In_ const char* switchTypeStr,
                    _Out_ sai_vs_switch_type_t& switchType);

            static bool parseBootType(
                    _In_ const char* bootTypeStr,
                    _Out_ sai_vs_boot_type_t& bootType);

            static bool parseUseTapDevice(
                    _In_ const char* useTapDeviceStr);

        public:

            sai_vs_switch_type_t m_switchType;

            sai_vs_boot_type_t m_bootType;

            uint32_t m_switchIndex;

            std::string m_hardwareInfo;

            bool m_useTapDevice;

            std::shared_ptr<LaneMap> m_laneMap;

            std::shared_ptr<EventQueue> m_eventQueue;
    };
}
