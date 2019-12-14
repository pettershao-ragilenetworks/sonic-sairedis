#include "RedisVidIndexGenerator.h"

#include "swss/logger.h"

using namespace sairedis;

RedisVidIndexGenerator::RedisVidIndexGenerator(
        _In_ std::shared_ptr<swss::DBConnector> dbConnector,
        _In_ const std::string& vidCounterName):
    m_dbConnector(dbConnector),
    m_vidCounterName(vidCounterName)
{
    SWSS_LOG_ENTER();

    m_redisClient = std::make_shared<swss::RedisClient>(m_dbConnector.get());
}

uint64_t RedisVidIndexGenerator::increment()
{
    SWSS_LOG_ENTER();

    // this counter must be atomic since it can be independently accessed by
    // sairedis and sycd

    return m_redisClient->incr(m_vidCounterName); // "VIDCOUNTER"
}

void RedisVidIndexGenerator::reset()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_ERROR("not implemented");
}
