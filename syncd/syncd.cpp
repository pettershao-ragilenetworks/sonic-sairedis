#include "syncd.h"
#include "sairediscommon.h"

#include "TimerWatchdog.h"
#include "CommandLineOptionsParser.h"
#include "PortMapParser.h"
#include "VidManager.h"
#include "FlexCounterManager.h"
#include "HardReiniter.h"
#include "NotificationProcessor.h"
#include "NotificationHandler.h"
#include "VirtualOidTranslator.h"
#include "ServiceMethodTable.h"
#include "SwitchNotifications.h"
#include "VirtualObjectIdManager.h"
#include "RedisVidIndexGenerator.h"
#include "Syncd.h"
#include "RequestShutdown.h"
#include "ComparisonLogic.h"

#include "meta/sai_serialize.h"

#include "swss/notificationconsumer.h"
#include "swss/select.h"
#include "swss/tokenize.h"
#include "swss/warm_restart.h"
#include "swss/table.h"
#include "swss/redisapi.h"


#include <inttypes.h>
#include <limits.h>

#include <iostream>
#include <map>
#include <unordered_map>

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

std::shared_ptr<sairedis::VirtualObjectIdManager> g_virtualObjectIdManager;

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


/*
 * SAI switch global needed for RPC server and for remove_switch
 */
sai_object_id_t gSwitchId = SAI_NULL_OBJECT_ID;

// TODO we must be sure that all threads and notifications will be stopped
// before destructor will be called on those objects

std::shared_ptr<VirtualOidTranslator> g_translator; // TODO move to syncd object

std::shared_ptr<CommandLineOptions> g_commandLineOptions; // TODO move to syncd object

bool g_veryFirstRun = false;

void notify_OA_about_syncd_exception()
{
    SWSS_LOG_ENTER();

    try
    {
        if (notifications != NULL)
        {
            std::vector<swss::FieldValueTuple> entry;

            SWSS_LOG_NOTICE("sending switch_shutdown_request notification to OA");

            notifications->send("switch_shutdown_request", "", entry);

            SWSS_LOG_NOTICE("notification send successfull");
        }
    }
    catch(const std::exception &e)
    {
        SWSS_LOG_ERROR("Runtime error: %s", e.what());
    }
    catch(...)
    {
        SWSS_LOG_ERROR("Unknown runtime error");
    }
}

typedef enum _syncd_restart_type_t
{
    SYNCD_RESTART_TYPE_COLD,

    SYNCD_RESTART_TYPE_WARM,

    SYNCD_RESTART_TYPE_FAST,

    SYNCD_RESTART_TYPE_PRE_SHUTDOWN,

} syncd_restart_type_t;

syncd_restart_type_t handleRestartQuery(
        _In_ swss::NotificationConsumer &restartQuery)
{
    SWSS_LOG_ENTER();

    std::string op;
    std::string data;
    std::vector<swss::FieldValueTuple> values;

    restartQuery.pop(op, data, values);

    SWSS_LOG_DEBUG("op = %s", op.c_str());

    if (op == "COLD")
    {
        SWSS_LOG_NOTICE("received COLD switch shutdown event");
        return SYNCD_RESTART_TYPE_COLD;
    }

    if (op == "WARM")
    {
        SWSS_LOG_NOTICE("received WARM switch shutdown event");
        return SYNCD_RESTART_TYPE_WARM;
    }

    if (op == "FAST")
    {
        SWSS_LOG_NOTICE("received FAST switch shutdown event");
        return SYNCD_RESTART_TYPE_FAST;
    }

    if (op == "PRE-SHUTDOWN")
    {
        SWSS_LOG_NOTICE("received PRE_SHUTDOWN switch event");
        return SYNCD_RESTART_TYPE_PRE_SHUTDOWN;
    }

    SWSS_LOG_WARN("received '%s' unknown switch shutdown event, assuming COLD", op.c_str());
    return SYNCD_RESTART_TYPE_COLD;
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

static void saiLoglevelNotify(
        _In_ std::string strApi,
        _In_ std::string strLogLevel)
{
    SWSS_LOG_ENTER();

    try
    {
        sai_log_level_t logLevel;
        sai_deserialize_log_level(strLogLevel, logLevel);

        sai_api_t api;
        sai_deserialize_api(strApi, api);

        sai_status_t status = g_vendorSai->logSet(api, logLevel);

        if (status == SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_NOTICE("Setting SAI loglevel %s on %s", strLogLevel.c_str(), strApi.c_str());
        }
        else
        {
            SWSS_LOG_INFO("set loglevel failed: %s", sai_serialize_status(status).c_str());
        }
    }
    catch (const std::exception& e)
    {
        SWSS_LOG_ERROR("Failed to set loglevel to %s on %s: %s",
                strLogLevel.c_str(),
                strApi.c_str(),
                e.what());
    }
}

void set_sai_api_loglevel()
{
    SWSS_LOG_ENTER();

    // We start from 1 since 0 is SAI_API_UNSPECIFIED.

    for (uint32_t idx = 1; idx < sai_metadata_enum_sai_api_t.valuescount; ++idx)
    {
        // TODO std::function<void(void)> f = std::bind(&Foo::doSomething, this);
        swss::Logger::linkToDb(
                sai_metadata_enum_sai_api_t.valuesnames[idx],
                saiLoglevelNotify,
                sai_serialize_log_level(SAI_LOG_LEVEL_NOTICE));
    }
}

void set_sai_api_log_min_prio(const std::string &prioStr)
{
    SWSS_LOG_ENTER();

    // We start from 1 since 0 is SAI_API_UNSPECIFIED.

    for (uint32_t idx = 1; idx < sai_metadata_enum_sai_api_t.valuescount; ++idx)
    {
        const auto& api_name = sai_metadata_enum_sai_api_t.valuesnames[idx];
        saiLoglevelNotify(api_name, prioStr);
    }
}

void sai_meta_log_syncd(
        _In_ sai_log_level_t log_level,
        _In_ const char *file,
        _In_ int line,
        _In_ const char *func,
        _In_ const char *format,
        ...)
    __attribute__ ((format (printf, 5, 6)));

void sai_meta_log_syncd(
        _In_ sai_log_level_t log_level,
        _In_ const char *file,
        _In_ int line,
        _In_ const char *func,
        _In_ const char *format,
        ...)
{
    // SWSS_LOG_ENTER() is omitted since this is logging for metadata

    char buffer[0x1000];

    va_list ap;
    va_start(ap, format);
    vsnprintf(buffer, 0x1000, format, ap);
    va_end(ap);

    swss::Logger::Priority p = swss::Logger::SWSS_NOTICE;

    switch (log_level)
    {
        case SAI_LOG_LEVEL_DEBUG:
            p = swss::Logger::SWSS_DEBUG;
            break;
        case SAI_LOG_LEVEL_INFO:
            p = swss::Logger::SWSS_INFO;
            break;
        case SAI_LOG_LEVEL_ERROR:
            p = swss::Logger::SWSS_ERROR;
            fprintf(stderr, "ERROR: %s: %s", func, buffer);
            break;
        case SAI_LOG_LEVEL_WARN:
            p = swss::Logger::SWSS_WARN;
            fprintf(stderr, "WARN: %s: %s", func, buffer);
            break;
        case SAI_LOG_LEVEL_CRITICAL:
            p = swss::Logger::SWSS_CRIT;
            break;

        default:
            p = swss::Logger::SWSS_NOTICE;
            break;
    }

    swss::Logger::getInstance().write(p, ":- %s: %s", func, buffer);
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

extern std::shared_ptr<Syncd> g_syncd;

int syncd_main(int argc, char **argv)
{
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    SWSS_LOG_ENTER();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);

    set_sai_api_loglevel();

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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
    sai_metadata_log = &sai_meta_log_syncd;
#pragma GCC diagnostic pop

    // TODO move to syncd object
    g_commandLineOptions = CommandLineOptionsParser::parseCommandLine(argc, argv);

    g_syncd = std::make_shared<Syncd>(g_vendorSai, g_commandLineOptions, isWarmStart);

    SWSS_LOG_NOTICE("command line: %s", g_commandLineOptions->getCommandLineString().c_str());

#ifdef SAITHRIFT
    if (g_commandLineOptions->m_portMapFile.size() > 0)
    {
        auto map = PortMapParser::parsePortMap(g_commandLineOptions->m_portMapFile);

        PortMap::setGlobalPortMap(map);
    }
#endif // SAITHRIFT

    // we need STATE_DB ASIC_DB and COUNTERS_DB

    dbAsic = std::make_shared<swss::DBConnector>("ASIC_DB", 0);
    std::shared_ptr<swss::DBConnector> dbNtf = std::make_shared<swss::DBConnector>("ASIC_DB", 0);
    std::shared_ptr<swss::DBConnector> dbState = std::make_shared<swss::DBConnector>("STATE_DB", 0);
    std::shared_ptr<swss::Table> warmRestartTable = std::make_shared<swss::Table>(dbState.get(), STATE_WARM_RESTART_TABLE_NAME);

    g_redisClient = std::make_shared<swss::RedisClient>(dbAsic.get());

    std::shared_ptr<swss::ConsumerTable> asicState = std::make_shared<swss::ConsumerTable>(dbAsic.get(), ASIC_STATE_TABLE);
    std::shared_ptr<swss::NotificationConsumer> restartQuery = std::make_shared<swss::NotificationConsumer>(dbAsic.get(), SYNCD_NOTIFICATION_CHANNEL_RESTARTQUERY);

    // TODO to be moved to ASIC_DB
    std::shared_ptr<swss::DBConnector> dbFlexCounter = std::make_shared<swss::DBConnector>("FLEX_COUNTER_DB", 0);
    std::shared_ptr<swss::ConsumerTable> flexCounter = std::make_shared<swss::ConsumerTable>(dbFlexCounter.get(), FLEX_COUNTER_TABLE);
    std::shared_ptr<swss::ConsumerTable> flexCounterGroup = std::make_shared<swss::ConsumerTable>(dbFlexCounter.get(), FLEX_COUNTER_GROUP_TABLE);

    // TODO move to syncd object
    g_translator = std::make_shared<VirtualOidTranslator>();

    auto switchConfigContainer = std::make_shared<sairedis::SwitchConfigContainer>();
    auto redisVidIndexGenerator = std::make_shared<sairedis::RedisVidIndexGenerator>(dbAsic, REDIS_KEY_VIDCOUNTER);

    g_virtualObjectIdManager =
        std::make_shared<sairedis::VirtualObjectIdManager>(
                0, // TODO global context, get from command line
                switchConfigContainer,
                redisVidIndexGenerator);

    /*
     * At the end we cant use producer consumer concept since if one process
     * will restart there may be something in the queue also "remove" from
     * response queue will also trigger another "response".
     */

    g_syncd->m_getResponse  = std::make_shared<swss::ProducerTable>(dbAsic.get(), "GETRESPONSE");
    notifications = std::make_shared<swss::NotificationProducer>(dbNtf.get(), "NOTIFICATIONS");

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
    if (g_commandLineOptions->m_runRPCServer)
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
        SWSS_LOG_NOTICE("before onSyncdStart");
        g_syncd->onSyncdStart(g_commandLineOptions->m_startType == SAI_START_TYPE_WARM_BOOT);
        SWSS_LOG_NOTICE("after onSyncdStart");

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

        notify_OA_about_syncd_exception();

        s = std::make_shared<swss::Select>();

        s->addSelectable(restartQuery.get());

        SWSS_LOG_NOTICE("starting main loop, ONLY restart query");

        if (g_commandLineOptions->m_disableExitSleep)
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

                SWSS_LOG_NOTICE("is asic queue empty: %d",asicState->empty());

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

                sai_attribute_t attr;

                attr.id = SAI_SWITCH_ATTR_RESTART_WARM;
                attr.value.booldata = true;

                status = g_vendorSai->set(SAI_OBJECT_TYPE_SWITCH, gSwitchId, &attr);

                if (status != SAI_STATUS_SUCCESS)
                {
                    SWSS_LOG_ERROR("Failed to set SAI_SWITCH_ATTR_RESTART_WARM=true: %s for pre-shutdown",
                            sai_serialize_status(status).c_str());

                    shutdownType = SYNCD_RESTART_TYPE_COLD;

                    warmRestartTable->hset("warm-shutdown", "state", "set-flag-failed");
                    continue;
                }

                attr.id = SAI_SWITCH_ATTR_PRE_SHUTDOWN;
                attr.value.booldata = true;

                status = g_vendorSai->set(SAI_OBJECT_TYPE_SWITCH, gSwitchId, &attr);

                if (status == SAI_STATUS_SUCCESS)
                {
                    warmRestartTable->hset("warm-shutdown", "state", "pre-shutdown-succeeded");

                    s = std::make_shared<swss::Select>(); // make sure previous select is destroyed

                    s->addSelectable(restartQuery.get());

                    SWSS_LOG_NOTICE("switched to PRE_SHUTDOWN, from now on accepting only shurdown requests");
                }
                else
                {
                    SWSS_LOG_ERROR("Failed to set SAI_SWITCH_ATTR_PRE_SHUTDOWN=true: %s",
                            sai_serialize_status(status).c_str());

                    warmRestartTable->hset("warm-shutdown", "state", "pre-shutdown-failed");

                    // Restore cold shutdown.
                    attr.id = SAI_SWITCH_ATTR_RESTART_WARM;
                    attr.value.booldata = false;
                    status = g_vendorSai->set(SAI_OBJECT_TYPE_SWITCH, gSwitchId, &attr);
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
            else if (result == swss::Select::OBJECT)
            {
                g_syncd->processEvent(*(swss::ConsumerTable*)sel);
            }

            twd.setEndTime();
        }
        catch(const std::exception &e)
        {
            SWSS_LOG_ERROR("Runtime error: %s", e.what());

            notify_OA_about_syncd_exception();

            s = std::make_shared<swss::Select>();

            s->addSelectable(restartQuery.get());

            if (g_commandLineOptions->m_disableExitSleep)
                runMainLoop = false;

            // make sure that if second exception will arise, then we break the loop
            g_commandLineOptions->m_disableExitSleep = true;

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
            warmRestartTable->hset("warm-shutdown", "state", "warm-shutdown-failed");
        }
        else
        {
            SWSS_LOG_NOTICE("Warm Reboot requested, keeping data plane running");

            sai_attribute_t attr;

            attr.id = SAI_SWITCH_ATTR_RESTART_WARM;
            attr.value.booldata = true;

            status = g_vendorSai->set(SAI_OBJECT_TYPE_SWITCH, gSwitchId, &attr);

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("Failed to set SAI_SWITCH_ATTR_RESTART_WARM=true: %s, fall back to cold restart",
                        sai_serialize_status(status).c_str());
                shutdownType = SYNCD_RESTART_TYPE_COLD;
                warmRestartTable->hset("warm-shutdown", "state", "set-flag-failed");
            }
        }
    }

    SWSS_LOG_NOTICE("Removing the switch gSwitchId=0x%" PRIx64, gSwitchId);

#ifdef SAI_SUPPORT_UNINIT_DATA_PLANE_ON_REMOVAL

    if (shutdownType == SYNCD_RESTART_TYPE_FAST || shutdownType == SYNCD_RESTART_TYPE_WARM)
    {
        SWSS_LOG_NOTICE("Fast/warm reboot requested, keeping data plane running");

        sai_attribute_t attr;

        attr.id = SAI_SWITCH_ATTR_UNINIT_DATA_PLANE_ON_REMOVAL;
        attr.value.booldata = false;

        status = g_vendorSai->set(SAI_OBJECT_TYPE_SWITCH, gSwitchId, &attr);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to set SAI_SWITCH_ATTR_UNINIT_DATA_PLANE_ON_REMOVAL=false: %s",
                    sai_serialize_status(status).c_str());
        }
    }

#endif

    g_syncd->m_manager->removeAllCounters();

    {
        SWSS_LOG_TIMER("remove switch");
        status = g_vendorSai->remove(SAI_OBJECT_TYPE_SWITCH, gSwitchId);
    }

    // Stop notification thread after removing switch
    g_processor->stopNotificationsProcessingThread();

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_NOTICE("Can't delete a switch. gSwitchId=0x%" PRIx64 " status=%s", gSwitchId,
                sai_serialize_status(status).c_str());
    }

    if (shutdownType == SYNCD_RESTART_TYPE_WARM)
    {
        warmRestartTable->hset("warm-shutdown", "state",
                              (status == SAI_STATUS_SUCCESS) ?
                              "warm-shutdown-succeeded":
                              "warm-shutdown-failed");
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
