#include "SwitchConfig.h"

#include "swss/logger.h"

using namespace sairedis;

SwitchConfig::SwitchConfig():
    m_switchIndex(0),
    m_hardwareInfo("")
{
    SWSS_LOG_ENTER();

    // empty
}
