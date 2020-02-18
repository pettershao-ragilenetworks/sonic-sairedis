#include "sairediscommon.h"

#include "TimerWatchdog.h"
#include "CommandLineOptionsParser.h"
#include "NotificationProcessor.h"
#include "NotificationHandler.h"
#include "ServiceMethodTable.h"
#include "SwitchNotifications.h"
#include "VirtualObjectIdManager.h"
#include "RedisVidIndexGenerator.h"
#include "Syncd.h"
#include "RequestShutdown.h"
#include "MetadataLogger.h"
#include "WarmRestartTable.h"
#include "RedisClient.h"

#include "meta/sai_serialize.h"

#include "swss/notificationproducer.h"
#include "swss/notificationconsumer.h"
#include "swss/select.h"
#include "swss/warm_restart.h"

#include <inttypes.h>

#ifdef SAITHRIFT
#define SWITCH_SAI_THRIFT_RPC_SERVER_PORT 9092
#endif // SAITHRIFT

using namespace syncd;
using namespace std::placeholders;

/*
 * Make sure that notification queue pointer is populated before we start
 * thread, and before we create_switch, since at switch_create we can start
 * receiving fdb_notifications which will arrive on different thread and
 * will call getQueueSize() when queue pointer could be null (this=0x0).
 */

std::shared_ptr<swss::NotificationProducer> g_notifications;

// TODO we must be sure that all threads and notifications will be stopped
// before destructor will be called on those objects

syncd_restart_type_t handleRestartQuery(
        _In_ swss::NotificationConsumer &restartQuery)
{
    SWSS_LOG_ENTER();

    std::string op;
    std::string data;
    std::vector<swss::FieldValueTuple> values;

    restartQuery.pop(op, data, values);

    SWSS_LOG_NOTICE("received %s switch shutdown event", op.c_str());

    return RequestShutdownCommandLineOptions::stringToRestartType(op);
}

void timerWatchdogCallback(
        _In_ int64_t span)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_ERROR("main loop execution exceeded %ld ms", span);
}

int syncd_main(int argc, char **argv)
{
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    SWSS_LOG_ENTER();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);

    swss::Logger::linkToDbNative("syncd");

    swss::WarmStart::initialize("syncd", "syncd");
    swss::WarmStart::checkWarmStart("syncd", "syncd");

    bool isWarmStart = swss::WarmStart::isWarmStart();

    MetadataLogger::initialize();

    auto commandLineOptions = CommandLineOptionsParser::parseCommandLine(argc, argv);

#ifdef SAITHRIFT

    if (commandLineOptions->m_portMapFile.size() > 0)
    {
        auto map = PortMapParser::parsePortMap(commandLineOptions->m_portMapFile);

        PortMap::setGlobalPortMap(map);
    }

#endif // SAITHRIFT

    auto vendorSai = std::make_shared<VendorSai>();

    auto g_syncd = std::make_shared<Syncd>(vendorSai, commandLineOptions, isWarmStart);

    /////////////////

    SWSS_LOG_NOTICE("command line: %s", commandLineOptions->getCommandLineString().c_str());

    // we need STATE_DB ASIC_DB and COUNTERS_DB

    auto dbAsic = std::make_shared<swss::DBConnector>("ASIC_DB", 0);

    std::shared_ptr<swss::DBConnector> dbNtf = std::make_shared<swss::DBConnector>("ASIC_DB", 0);

    g_syncd->m_client = std::make_shared<RedisClient>(dbAsic);

    g_syncd->m_processor = std::make_shared<NotificationProcessor>(g_syncd->m_client, std::bind(&Syncd::syncProcessNotification, g_syncd.get(), _1));
    g_syncd->m_handler = std::make_shared<NotificationHandler>(g_syncd->m_processor);

    SwitchNotifications sn;

    sn.onFdbEvent = std::bind(&NotificationHandler::onFdbEvent, g_syncd->m_handler.get(), _1, _2);
    sn.onPortStateChange = std::bind(&NotificationHandler::onPortStateChange, g_syncd->m_handler.get(), _1, _2);
    sn.onQueuePfcDeadlock = std::bind(&NotificationHandler::onQueuePfcDeadlock, g_syncd->m_handler.get(), _1, _2);
    sn.onSwitchShutdownRequest = std::bind(&NotificationHandler::onSwitchShutdownRequest, g_syncd->m_handler.get(), _1);
    sn.onSwitchStateChange = std::bind(&NotificationHandler::onSwitchStateChange, g_syncd->m_handler.get(), _1, _2);

    g_syncd->m_handler->setSwitchNotifications(sn.getSwitchNotifications());

    std::shared_ptr<swss::ConsumerTable> asicState = std::make_shared<swss::ConsumerTable>(dbAsic.get(), ASIC_STATE_TABLE);
    std::shared_ptr<swss::NotificationConsumer> restartQuery = std::make_shared<swss::NotificationConsumer>(dbAsic.get(), SYNCD_NOTIFICATION_CHANNEL_RESTARTQUERY);

    // TODO to be moved to ASIC_DB
    std::shared_ptr<swss::DBConnector> dbFlexCounter = std::make_shared<swss::DBConnector>("FLEX_COUNTER_DB", 0);
    std::shared_ptr<swss::ConsumerTable> flexCounter = std::make_shared<swss::ConsumerTable>(dbFlexCounter.get(), FLEX_COUNTER_TABLE);
    std::shared_ptr<swss::ConsumerTable> flexCounterGroup = std::make_shared<swss::ConsumerTable>(dbFlexCounter.get(), FLEX_COUNTER_GROUP_TABLE);

    auto switchConfigContainer = std::make_shared<sairedis::SwitchConfigContainer>();
    auto redisVidIndexGenerator = std::make_shared<sairedis::RedisVidIndexGenerator>(dbAsic, REDIS_KEY_VIDCOUNTER);

    auto virtualObjectIdManager =
        std::make_shared<sairedis::VirtualObjectIdManager>(
                0, // TODO global context, get from command line
                switchConfigContainer,
                redisVidIndexGenerator);

    // TODO move to syncd object
    g_syncd->m_translator = std::make_shared<VirtualOidTranslator>(g_syncd->m_client, virtualObjectIdManager,  vendorSai);

    g_syncd->m_processor->m_translator = g_syncd->m_translator; // TODO as param

    /*
     * At the end we cant use producer consumer concept since if one process
     * will restart there may be something in the queue also "remove" from
     * response queue will also trigger another "response".
     */

    g_syncd->m_getResponse  = std::make_shared<swss::ProducerTable>(dbAsic.get(), REDIS_TABLE_GETRESPONSE);
    g_notifications = std::make_shared<swss::NotificationProducer>(dbNtf.get(), REDIS_TABLE_NOTIFICATIONS);

    g_syncd->m_veryFirstRun = g_syncd->isVeryFirstRun();

    g_syncd->performStartupLogic();

    ServiceMethodTable smt;

    smt.profileGetValue = std::bind(&Syncd::profileGetValue, g_syncd.get(), _1, _2);
    smt.profileGetNextValue = std::bind(&Syncd::profileGetNextValue, g_syncd.get(), _1, _2, _3);

    auto test_services = smt.getServiceMethodTable();

    WarmRestartTable warmRestartTable("STATE_DB");

    // TODO need per api test service method table static template
    sai_status_t status = vendorSai->initialize(0, &test_services);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("fail to sai_api_initialize: %s",
                sai_serialize_status(status).c_str());
        return EXIT_FAILURE;
    }

#ifdef SAITHRIFT
    if (commandLineOptions->m_runRPCServer)
    {
        start_sai_thrift_rpc_server(SWITCH_SAI_THRIFT_RPC_SERVER_PORT);
        SWSS_LOG_NOTICE("rpcserver started");
    }
#endif // SAITHRIFT

    SWSS_LOG_NOTICE("syncd started");

    syncd_restart_type_t shutdownType = SYNCD_RESTART_TYPE_COLD;

    volatile bool runMainLoop = true;

    std::shared_ptr<swss::Select> s = std::make_shared<swss::Select>();

    try
    {
        g_syncd->onSyncdStart(commandLineOptions->m_startType == SAI_START_TYPE_WARM_BOOT);

        // create notifications processing thread after we create_switch to
        // make sure, we have switch_id translated to VID before we start
        // processing possible quick fdb notifications, and pointer for
        // notification queue is created before we create switch
        g_syncd->m_processor->startNotificationsProcessingThread();

        SWSS_LOG_NOTICE("syncd listening for events");

        s->addSelectable(asicState.get());
        s->addSelectable(restartQuery.get());
        s->addSelectable(flexCounter.get());
        s->addSelectable(flexCounterGroup.get());

        SWSS_LOG_NOTICE("starting main loop");
    }
    catch(const std::exception &e)
    {
        SWSS_LOG_ERROR("Runtime error during syncd init: %s", e.what());

        g_syncd->sendShutdownRequestAfterException();

        s = std::make_shared<swss::Select>();

        s->addSelectable(restartQuery.get());

        SWSS_LOG_NOTICE("starting main loop, ONLY restart query");

        if (commandLineOptions->m_disableExitSleep)
            runMainLoop = false;
    }

    TimerWatchdog twd(30 * 1000000); // watch for executions over 30 seconds

    twd.setCallback(timerWatchdogCallback);

    while(runMainLoop)
    {
        try
        {
            swss::Selectable *sel = NULL;

            int result = s->select(&sel);

            twd.setStartTime();

            if (sel == restartQuery.get())
            {
                /*
                 * This is actual a bad design, since selectable may pick up
                 * multiple events from the queue, and after restart those
                 * events will be forgotten since they were consumed already and
                 * this may lead to forget populate object table which will
                 * lead to unable to find some objects.
                 */

                SWSS_LOG_NOTICE("is asic queue empty: %d", asicState->empty());

                while (!asicState->empty())
                {
                    g_syncd->processEvent(*asicState.get());
                }

                SWSS_LOG_NOTICE("drained queue");

                shutdownType = handleRestartQuery(*restartQuery);

                if (shutdownType != SYNCD_RESTART_TYPE_PRE_SHUTDOWN)
                {
                    // break out the event handling loop to shutdown syncd
                    runMainLoop = false;
                    break;
                }

                // Handle switch pre-shutdown and wait for the final shutdown
                // event

                SWSS_LOG_TIMER("warm pre-shutdown");

                g_syncd->m_manager->removeAllCounters();

                status = g_syncd->setRestartWarmOnAllSwitches(true);

                if (status != SAI_STATUS_SUCCESS)
                {
                    SWSS_LOG_ERROR("Failed to set SAI_SWITCH_ATTR_RESTART_WARM=true: %s for pre-shutdown",
                            sai_serialize_status(status).c_str());

                    shutdownType = SYNCD_RESTART_TYPE_COLD;

                    warmRestartTable.setFlagFailed();
                    continue;
                }

                status = g_syncd->setPreShutdownOnAllSwitches();

                if (status == SAI_STATUS_SUCCESS)
                {
                    warmRestartTable.setPreShutdown(true);

                    s = std::make_shared<swss::Select>(); // make sure previous select is destroyed

                    s->addSelectable(restartQuery.get());

                    SWSS_LOG_NOTICE("switched to PRE_SHUTDOWN, from now on accepting only shurdown requests");
                }
                else
                {
                    SWSS_LOG_ERROR("Failed to set SAI_SWITCH_ATTR_PRE_SHUTDOWN=true: %s",
                            sai_serialize_status(status).c_str());

                    warmRestartTable.setPreShutdown(false);

                    // Restore cold shutdown.

                    g_syncd->setRestartWarmOnAllSwitches(false);
                }
            }
            else if (sel == flexCounter.get())
            {
                g_syncd->processFlexCounterEvent(*(swss::ConsumerTable*)sel);
            }
            else if (sel == flexCounterGroup.get())
            {
                g_syncd->processFlexCounterGroupEvent(*(swss::ConsumerTable*)sel);
            }
            else if (sel == asicState.get())
            {
                g_syncd->processEvent(*(swss::ConsumerTable*)sel);
            }
            else
            {
                SWSS_LOG_ERROR("select failed: %d", result);
            }

            twd.setEndTime();
        }
        catch(const std::exception &e)
        {
            SWSS_LOG_ERROR("Runtime error: %s", e.what());

            g_syncd->sendShutdownRequestAfterException();

            s = std::make_shared<swss::Select>();

            s->addSelectable(restartQuery.get());

            if (commandLineOptions->m_disableExitSleep)
                runMainLoop = false;

            // make sure that if second exception will arise, then we break the loop
            commandLineOptions->m_disableExitSleep = true;

            twd.setEndTime();
        }
    }

    if (shutdownType == SYNCD_RESTART_TYPE_WARM)
    {
        const char *warmBootWriteFile = g_syncd->profileGetValue(0, SAI_KEY_WARM_BOOT_WRITE_FILE);

        SWSS_LOG_NOTICE("using warmBootWriteFile: '%s'", warmBootWriteFile);

        if (warmBootWriteFile == NULL)
        {
            SWSS_LOG_WARN("user requested warm shutdown but warmBootWriteFile is not specified, forcing cold shutdown");

            shutdownType = SYNCD_RESTART_TYPE_COLD;
            warmRestartTable.setWarmShutdown(false);
        }
        else
        {
            SWSS_LOG_NOTICE("Warm Reboot requested, keeping data plane running");

            status = g_syncd->setRestartWarmOnAllSwitches(true);

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("Failed to set SAI_SWITCH_ATTR_RESTART_WARM=true: %s, fall back to cold restart",
                        sai_serialize_status(status).c_str());

                shutdownType = SYNCD_RESTART_TYPE_COLD;

                warmRestartTable.setFlagFailed();
            }
        }
    }

#ifdef SAI_SUPPORT_UNINIT_DATA_PLANE_ON_REMOVAL

    if (shutdownType == SYNCD_RESTART_TYPE_FAST || shutdownType == SYNCD_RESTART_TYPE_WARM)
    {
        g_syncd->setUninitDataPlaneOnRemovalOnAllSwitches();
    }

#endif

    g_syncd->m_manager->removeAllCounters();

    status = g_syncd->removeAllSwitches();

    // Stop notification thread after removing switch
    g_syncd->m_processor->stopNotificationsProcessingThread();

    if (shutdownType == SYNCD_RESTART_TYPE_WARM)
    {
        warmRestartTable.setWarmShutdown(status == SAI_STATUS_SUCCESS);
    }

    SWSS_LOG_NOTICE("calling api uninitialize");

    status = vendorSai->uninitialize();

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to uninitialize api: %s", sai_serialize_status(status).c_str());
    }

    SWSS_LOG_NOTICE("uninitialize finished");

    return EXIT_SUCCESS;
}
