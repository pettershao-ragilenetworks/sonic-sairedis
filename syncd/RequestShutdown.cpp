#include "RequestShutdown.h"

#include "swss/logger.h"
#include "swss/notificationproducer.h"

#include <iostream>

using namespace syncd;

RequestShutdown::RequestShutdown(
        _In_ std::shared_ptr<RequestShutdownCommandLineOptions> options):
    m_options(options)
{
    SWSS_LOG_ENTER();

    // empty
}

RequestShutdown::~RequestShutdown()
{
    SWSS_LOG_ENTER();

    // empty
}

void RequestShutdown::send()
{
    SWSS_LOG_ENTER();

    // TODO with multiple syncd will need to load config and global context flag

    swss::DBConnector db("ASIC_DB", 0);
    swss::NotificationProducer restartQuery(&db, SYNCD_NOTIFICATION_CHANNEL_RESTARTQUERY);

    std::vector<swss::FieldValueTuple> values;

    auto op = RequestShutdownCommandLineOptions::restartTypeToString(m_options->getRestartType());

    SWSS_LOG_NOTICE("requested %s shutdown", op.c_str());

    std::cerr << "requested " << op << " shutdown" << std::endl;

    restartQuery.send(op, op, values);
}
