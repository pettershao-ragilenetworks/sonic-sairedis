#include "Syncd.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"

using namespace syncd;

Syncd::Syncd(
        _In_ std::shared_ptr<CommandLineOptions> cmd,
        _In_ bool isWarmStart):
    m_commandLineOptions(cmd),
    m_isWarmStart(isWarmStart),
    m_asicInitViewMode(false) // by default we are in APPLY view mode
{
    SWSS_LOG_ENTER();
}

Syncd::~Syncd()
{
    SWSS_LOG_ENTER();

}


bool Syncd::getAsicInitViewMode() const
{
    SWSS_LOG_ENTER();

    return m_asicInitViewMode;
}

void Syncd::setAsicInitViewMode(
        _In_ bool enable)
{
    SWSS_LOG_ENTER();

    m_asicInitViewMode = enable;
}
