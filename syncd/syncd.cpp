#include "syncd.h"
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

#include "meta/sai_serialize.h"

#include "swss/notificationconsumer.h"
#include "swss/select.h"
#include "swss/warm_restart.h"
#include "swss/redisapi.h"

#include <inttypes.h>

using namespace syncd;
using namespace std::placeholders;

std::string fdbFlushSha;

/*
 * Make sure that notification queue pointer is populated before we start
 * thread, and before we create_switch, since at switch_create we can start
 * receiving fdb_notifications which will arrive on different thread and
 * will call getQueueSize() when queue pointer could be null (this=0x0).
 */

std::shared_ptr<NotificationProcessor> g_processor = std::make_shared<NotificationProcessor>();
std::shared_ptr<NotificationHandler> g_handler = std::make_shared<NotificationHandler>(g_processor);

std::shared_ptr<sairedis::SaiInterface> g_vendorSai = std::make_shared<VendorSai>();

std::shared_ptr<Syncd> g_syncd;

/**
 * @brief Global mutex for thread synchronization
 *
 * Purpose of this mutex is to synchronize multiple threads like main thread,
 * counters and notifications as well as all operations which require multiple
 * Redis DB access.
 *
 * For example: query DB for next VID id number, and then put map RID and VID
 * to Redis. From syncd point of view this entire operation should be atomic
 * and no other thread should access DB or make assumption on previous
 * information until entire operation will finish.
 */
std::mutex g_mutex;

std::shared_ptr<swss::DBConnector>          dbAsic;
std::shared_ptr<swss::RedisClient>          g_redisClient;
std::shared_ptr<swss::NotificationProducer> notifications;

/**
 * @brief Contains map of all created switches.
 *
 * This syncd implementation supports only one switch but it's written in
 * a way that could be extended to use multiple switches in the future, some
 * refactoring needs to be made in marked places.
 *
 * To support multiple switches VIDTORID and RIDTOVID db entries needs to be
 * made per switch like HIDDEN and LANES. Best way is to wrap vid/rid map to
 * functions that will return right key.
 *
 * Key is switch VID.
 */
std::map<sai_object_id_t, std::shared_ptr<SaiSwitch>> switches;

// TODO we must be sure that all threads and notifications will be stopped
// before destructor will be called on those objects

std::shared_ptr<VirtualOidTranslator> g_translator; // TODO move to syncd object

bool g_veryFirstRun = false;

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

bool isVeryFirstRun()
{
    SWSS_LOG_ENTER();

    /*
     * If lane map is not defined in redis db then we assume this is very first
     * start of syncd later on we can add additional checks here.
     *
     * TODO: if we add more switches then we need lane maps per switch.
     * TODO: we also need other way to check if this is first start
     *
     * We could use VIDCOUNTER also, but if something is defined in the DB then
     * we assume this is not the first start.
     *
     * TODO we need to fix this, since when there will be queue, it will still think
     * this is first run, let's query HIDDEN ?
     */

    auto keys = g_redisClient->keys(HIDDEN);

    bool firstRun = keys.size() == 0;

    SWSS_LOG_NOTICE("First Run: %s", firstRun ? "True" : "False");

    return firstRun;
}


void timerWatchdogCallback(
        _In_ int64_t span)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_ERROR("main loop execution exceeded %ld ms", span);
}

void redisClearVidToRidMap()
{
    SWSS_LOG_ENTER();

    g_redisClient->del(VIDTORID);
}

void redisClearRidToVidMap()
{
    SWSS_LOG_ENTER();

    g_redisClient->del(RIDTOVID);
}

/**
 * When set to true extra logging will be added for tracking references.  This
 * is useful for debugging, but for production operations this will produce too
 * much noise in logs, and we still can replay scenario using recordings.
 */
bool enableRefernceCountLogs = false;

int syncd_main(int argc, char **argv)
{
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    SWSS_LOG_ENTER();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);

    swss::Logger::linkToDbNative("syncd"); // TODO fix also in discovery

    swss::WarmStart::initialize("syncd", "syncd");
    swss::WarmStart::checkWarmStart("syncd", "syncd");

    bool isWarmStart = swss::WarmStart::isWarmStart(); // since global, can be applied in main

    SwitchNotifications sn;

    sn.onFdbEvent = std::bind(&NotificationHandler::onFdbEvent, g_handler.get(), _1, _2);
    sn.onPortStateChange = std::bind(&NotificationHandler::onPortStateChange, g_handler.get(), _1, _2);
    sn.onQueuePfcDeadlock = std::bind(&NotificationHandler::onQueuePfcDeadlock, g_handler.get(), _1, _2);
    sn.onSwitchShutdownRequest = std::bind(&NotificationHandler::onSwitchShutdownRequest, g_handler.get(), _1);
    sn.onSwitchStateChange = std::bind(&NotificationHandler::onSwitchStateChange, g_handler.get(), _1, _2);

    g_handler->setSwitchNotifications(sn.getSwitchNotifications());

    MetadataLogger::initialize();

    // TODO move to syncd object
    auto commandLineOptions = CommandLineOptionsParser::parseCommandLine(argc, argv);

    g_syncd = std::make_shared<Syncd>(g_vendorSai, commandLineOptions, isWarmStart);

    SWSS_LOG_NOTICE("command line: %s", commandLineOptions->getCommandLineString().c_str());

#ifdef SAITHRIFT
    if (commandLineOptions->m_portMapFile.size() > 0)
    {
        auto map = PortMapParser::parsePortMap(commandLineOptions->m_portMapFile);

        PortMap::setGlobalPortMap(map);
    }
#endif // SAITHRIFT

    // we need STATE_DB ASIC_DB and COUNTERS_DB

    dbAsic = std::make_shared<swss::DBConnector>("ASIC_DB", 0);
    std::shared_ptr<swss::DBConnector> dbNtf = std::make_shared<swss::DBConnector>("ASIC_DB", 0);

    WarmRestartTable warmRestartTable("STATE_DB");

    g_redisClient = std::make_shared<swss::RedisClient>(dbAsic.get());

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
    g_translator = std::make_shared<VirtualOidTranslator>(virtualObjectIdManager);

    /*
     * At the end we cant use producer consumer concept since if one process
     * will restart there may be something in the queue also "remove" from
     * response queue will also trigger another "response".
     */

    g_syncd->m_getResponse  = std::make_shared<swss::ProducerTable>(dbAsic.get(), REDIS_TABLE_GETRESPONSE);
    notifications = std::make_shared<swss::NotificationProducer>(dbNtf.get(), REDIS_TABLE_NOTIFICATIONS);

    std::string fdbFlushLuaScript = swss::loadLuaScript("fdb_flush.lua");
    fdbFlushSha = swss::loadRedisScript(dbAsic.get(), fdbFlushLuaScript);

    g_veryFirstRun = isVeryFirstRun();

    g_syncd->performStartupLogic();

    ServiceMethodTable smt;

    smt.profileGetValue = std::bind(&Syncd::profileGetValue, g_syncd.get(), _1, _2);
    smt.profileGetNextValue = std::bind(&Syncd::profileGetNextValue, g_syncd.get(), _1, _2, _3);

    auto test_services = smt.getServiceMethodTable();

    // TODO need per api test service method table static template
    sai_status_t status = g_vendorSai->initialize(0, &test_services);

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
        g_processor->startNotificationsProcessingThread();

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
    g_processor->stopNotificationsProcessingThread();

    if (shutdownType == SYNCD_RESTART_TYPE_WARM)
    {
        warmRestartTable.setWarmShutdown(status == SAI_STATUS_SUCCESS);
    }

    SWSS_LOG_NOTICE("calling api uninitialize");

    status = g_vendorSai->uninitialize();

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to uninitialize api: %s", sai_serialize_status(status).c_str());
    }

    SWSS_LOG_NOTICE("uninitialize finished");

    return EXIT_SUCCESS;
}
