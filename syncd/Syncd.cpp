#include "Syncd.h"
#include "lib/inc/sairediscommon.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"

#include "syncd.h" // TODO to be removed

using namespace syncd;

Syncd::Syncd(
        _In_ std::shared_ptr<CommandLineOptions> cmd,
        _In_ bool isWarmStart):
    m_commandLineOptions(cmd),
    m_isWarmStart(isWarmStart),
    m_asicInitViewMode(false) // by default we are in APPLY view mode
{
    SWSS_LOG_ENTER();

    m_manager = std::make_shared<FlexCounterManager>();
}

Syncd::~Syncd()
{
    SWSS_LOG_ENTER();

    // empty
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

bool Syncd::isInitViewMode() const
{
    SWSS_LOG_ENTER();

    return m_asicInitViewMode && m_commandLineOptions->m_enableTempView;
}

sai_status_t processSingleEvent(
        _In_ const swss::KeyOpFieldsValuesTuple &kco);

void Syncd::processEvent(
        _In_ swss::ConsumerTable &consumer)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(g_mutex);

    do
    {
        swss::KeyOpFieldsValuesTuple kco;

        if (isInitViewMode())
        {
            /*
             * In init mode we put all data to TEMP view and we snoop.  We need
             * to specify temporary view prefix in consumer since consumer puts
             * data to redis db.
             */

            consumer.pop(kco, TEMP_PREFIX);
        }
        else
        {
            consumer.pop(kco);
        }

        processSingleEvent(kco);
    }
    while (!consumer.empty());
}


