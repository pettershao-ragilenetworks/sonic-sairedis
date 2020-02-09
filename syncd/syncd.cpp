#include "syncd.h"
#include "sairediscommon.h"
#include "swss/tokenize.h"

#include "swss/warm_restart.h"
#include "swss/table.h"
#include "swss/redisapi.h"

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

#include <inttypes.h>
#include <limits.h>

#include <iostream>
#include <map>
#include <unordered_map>

using namespace syncd;
using namespace std::placeholders;

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

std::string fdbFlushSha;
std::string fdbFlushLuaScriptName = "fdb_flush.lua";

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

void sai_diag_shell(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    sai_status_t status;

    /*
     * This is currently blocking API on broadcom, it will block until we exit
     * shell.
     */

    while (true)
    {
        sai_attribute_t attr;
        attr.id = SAI_SWITCH_ATTR_SWITCH_SHELL_ENABLE;
        attr.value.booldata = true;

        status = g_vendorSai->set(SAI_OBJECT_TYPE_SWITCH, switch_id, &attr);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to enable switch shell: %s",
                    sai_serialize_status(status).c_str());
            return;
        }

        sleep(1);
    }
}


// TODO combine all methods to 1
void startDiagShell(
        _In_ sai_object_id_t switchRid)
{
    SWSS_LOG_ENTER();

    if (g_commandLineOptions->m_enableDiagShell)
    {
        SWSS_LOG_NOTICE("starting diag shell thread");

        std::thread diag_shell_thread = std::thread(sai_diag_shell, switchRid);

        diag_shell_thread.detach();
    }
}

void sendNotifyResponse(
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    std::string strStatus = sai_serialize_status(status);

    std::vector<swss::FieldValueTuple> entry;

    SWSS_LOG_INFO("sending response: %s", strStatus.c_str());

    g_syncd->m_getResponse->set(strStatus, entry, REDIS_ASIC_STATE_COMMAND_NOTIFY);
}

void clearTempView()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("clearing current TEMP VIEW");

    SWSS_LOG_TIMER("clear temp view");

    std::string pattern = TEMP_PREFIX + (ASIC_STATE_TABLE + std::string(":*"));

    /*
     * TODO this must be ATOMIC, and could use lua script.
     *
     * We need to expose api to execute user lua script not only predefined.
     */


    for (const auto &key: g_redisClient->keys(pattern))
    {
        g_redisClient->del(key);
    }

    /*
     * Also clear list of objects removed in init view mode.
     */

    g_syncd->m_initViewRemovedVidSet.clear();
}

sai_status_t onApplyViewInFastFastBoot()
{
    SWSS_LOG_ENTER();

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_FAST_API_ENABLE;
    attr.value.booldata = false;

    sai_status_t status = g_vendorSai->set(SAI_OBJECT_TYPE_SWITCH, gSwitchId, &attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to set SAI_SWITCH_ATTR_FAST_API_ENABLE=false: %s", sai_serialize_status(status).c_str());
    }

    return status;
}

sai_status_t notifySyncd(
        _In_ const std::string& op)
{
    SWSS_LOG_ENTER();

    if (!g_commandLineOptions->m_enableTempView)
    {
        SWSS_LOG_NOTICE("received %s, ignored since TEMP VIEW is not used, returning success", op.c_str());

        sendNotifyResponse(SAI_STATUS_SUCCESS);

        return SAI_STATUS_SUCCESS;
    }

    static bool firstInitWasPerformed = false; // !!! TODO static !

    auto redisNotifySyncd = sai_deserialize_redis_notify_syncd(op);

    if (g_veryFirstRun && firstInitWasPerformed && redisNotifySyncd == SAI_REDIS_NOTIFY_SYNCD_INIT_VIEW)
    {
        /*
         * Make sure that when second INIT view arrives, then we will jump
         * to next section, since second init view may create switch that
         * already exists and will fail with creating multiple switches
         * error.
         */

        g_veryFirstRun = false;
    }
    else if (g_veryFirstRun)
    {
        SWSS_LOG_NOTICE("very first run is TRUE, op = %s", op.c_str());

        sai_status_t status = SAI_STATUS_SUCCESS;

        /*
         * On the very first start of syncd, "compile" view is directly applied
         * on device, since it will make it easier to switch to new asic state
         * later on when we restart orch agent.
         */

        if (redisNotifySyncd == SAI_REDIS_NOTIFY_SYNCD_INIT_VIEW)
        {
            /*
             * On first start we just do "apply" directly on asic so we set
             * init to false instead of true.
             */

            g_syncd->m_asicInitViewMode = false;

            firstInitWasPerformed = true;

            /*
             * We need to clear current temp view to make space for new one.
             */

            clearTempView();
        }
        else if (redisNotifySyncd == SAI_REDIS_NOTIFY_SYNCD_APPLY_VIEW)
        {
            g_veryFirstRun = false;

            g_syncd->m_asicInitViewMode = false;

            if (g_commandLineOptions->m_startType == SAI_START_TYPE_FASTFAST_BOOT)
            {
                /* fastfast boot configuration end */
                status = onApplyViewInFastFastBoot();
            }

            SWSS_LOG_NOTICE("setting very first run to FALSE, op = %s", op.c_str());
        }
        else
        {
            SWSS_LOG_THROW("unknown operation: %s", op.c_str());
        }

        sendNotifyResponse(status);

        return status;
    }

    if (redisNotifySyncd == SAI_REDIS_NOTIFY_SYNCD_INIT_VIEW)
    {
        if (g_syncd->m_asicInitViewMode)
        {
            SWSS_LOG_WARN("syncd is already in asic INIT VIEW mode, but received init again, orchagent restarted before apply?");
        }

        g_syncd->m_asicInitViewMode = true;

        clearTempView();

        /*
         * TODO: Currently as WARN to be easier to spot, later should be NOTICE.
         */

        SWSS_LOG_WARN("syncd switched to INIT VIEW mode, all op will be saved to TEMP view");

        sendNotifyResponse(SAI_STATUS_SUCCESS);
    }
    else if (redisNotifySyncd == SAI_REDIS_NOTIFY_SYNCD_APPLY_VIEW)
    {
        g_syncd->m_asicInitViewMode = false;

        /*
         * TODO: Currently as WARN to be easier to spot, later should be NOTICE.
         */

        SWSS_LOG_WARN("syncd received APPLY VIEW, will translate");

        sai_status_t status = syncdApplyView();

        sendNotifyResponse(status);

        if (status == SAI_STATUS_SUCCESS)
        {
            /*
             * We successfully applied new view, VID mapping could change, so we
             * need to clear local db, and all new VIDs will be queried using
             * redis.
             *
             * TODO possible race condition - get notification when new view is
             * applied and cache have old values, and notification start's
             * translating vid/rid, we need to stop processing notifications
             * for transition (queue can still grow), possible fdb
             * notifications but fdb learning was disabled on warm boot, so
             * there should be no issue
             */

            g_translator->clearLocalCache();
        }
        else
        {
            /*
             * Apply view failed. It can fail in 2 ways, ether nothing was
             * executed, on asic, or asic is inconsistent state then we should
             * die or hang
             */

            return status;
        }
    }
    else if (redisNotifySyncd == SAI_REDIS_NOTIFY_SYNCD_INSPECT_ASIC)
    {
        SWSS_LOG_NOTICE("syncd switched to INSPECT ASIC mode");

        g_syncd->inspectAsic();

        sendNotifyResponse(SAI_STATUS_SUCCESS);
    }
    else
    {
        SWSS_LOG_ERROR("unknown operation: %s", op.c_str());

        sendNotifyResponse(SAI_STATUS_NOT_IMPLEMENTED);

        SWSS_LOG_THROW("notify syncd %s operation failed", op.c_str());
    }

    return SAI_STATUS_SUCCESS;
}

void on_switch_create_in_init_view(
        _In_ sai_object_id_t switch_vid,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    /*
     * This needs to be refactored if we need multiple switch support.
     */

    /*
     * We can have multiple switches here, but each switch is identified by
     * SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO. This attribute is treated as key,
     * so each switch will have different hardware info.
     *
     * Currently we assume that we have only one switch.
     *
     * We can have 2 scenarios here:
     *
     * - we have multiple switches already existing, and in init view mode user
     *   will create the same switches, then since switch id are deterministic
     *   we can match them by hardware info and by switch id, it may happen
     *   that switch id will be different if user will create switches in
     *   different order, this case will be not supported unless special logic
     *   will be written to handle that case.
     *
     * - if user created switches but non of switch has the same hardware info
     *   then it means we need to create actual switch here, since user will
     *   want to query switch ports etc values, that's why on create switch is
     *   special case, and that's why we need to keep track of all switches
     *
     * Since we are creating switch here, we are sure that this switch don't
     * have any oid attributes set, so we can pass all attributes
     */

    /*
     * Multiple switches scenario with changed order:
     *
     * If orchagent will create the same switch with the same hardware info but
     * with different order since switch id is deterministic, then VID of both
     * switches will not match:
     *
     * First we can have INFO = "A" swid 0x00170000, INFO = "B" swid 0x01170001
     *
     * Then we can have INFO = "B" swid 0x00170000, INFO = "A" swid 0x01170001
     *
     * Currently we don't have good solution for that so we will throw in that case.
     */

    if (switches.size() == 0)
    {
        /*
         * There are no switches currently, so we need to create this switch so
         * user in init mode could query switch properties using GET api.
         *
         * We assume that none of attributes is object id attribute.
         *
         * This scenario can happen when you start syncd on empty database and
         * then you quit and restart it again.
         */

        sai_object_id_t switch_rid;

        sai_status_t status;

        {
            SWSS_LOG_TIMER("cold boot: create switch");
            status = g_vendorSai->create(SAI_OBJECT_TYPE_SWITCH, &switch_rid, 0, attr_count, attr_list);
        }

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_THROW("failed to create switch in init view mode: %s",
                    sai_serialize_status(status).c_str());
        }

#ifdef SAITHRIFT
        gSwitchId = switch_rid;
        SWSS_LOG_NOTICE("Initialize gSwitchId with ID = 0x%" PRIx64, gSwitchId);
#endif

        /*
         * Object was created so new object id was generated we
         * need to save virtual id's to redis db.
         */

        std::string str_vid = sai_serialize_object_id(switch_vid);
        std::string str_rid = sai_serialize_object_id(switch_rid);

        SWSS_LOG_NOTICE("created real switch VID %s to RID %s in init view mode", str_vid.c_str(), str_rid.c_str());

        g_translator->insertRidAndVid(switch_rid, switch_vid);

        /*
         * Make switch initialization and get all default data.
         */

        switches[switch_vid] = std::make_shared<SaiSwitch>(switch_vid, switch_rid);
    }
    else if (switches.size() == 1)
    {
        /*
         * There is already switch defined, we need to match it by hardware
         * info and we need to know that current switch VID also should match
         * since it's deterministic created.
         */

        auto sw = switches.begin()->second;

        /*
         * Switches VID must match, since it's deterministic.
         */

        if (switch_vid != sw->getVid())
        {
            SWSS_LOG_THROW("created switch VID don't match: previous %s, current: %s",
                    sai_serialize_object_id(switch_vid).c_str(),
                    sai_serialize_object_id(sw->getVid()).c_str());
        }

        /*
         * Also hardware info also must match.
         */

        std::string currentHw = sw->getHardwareInfo();
        std::string newHw;

        auto attr = sai_metadata_get_attr_by_id(SAI_SWITCH_ATTR_SWITCH_HARDWARE_INFO, attr_count, attr_list);

        if (attr == NULL)
        {
            /*
             * This is ok, attribute doesn't exist, so assumption is empty string.
             */
        }
        else
        {
            SWSS_LOG_DEBUG("new switch contains hardware info of length %u", attr->value.s8list.count);

            newHw = std::string((char*)attr->value.s8list.list, attr->value.s8list.count);
        }

        if (currentHw != newHw)
        {
            SWSS_LOG_THROW("hardware info missmatch: current '%s' vs new '%s'", currentHw.c_str(), newHw.c_str());
        }

        SWSS_LOG_NOTICE("current switch hardware info: '%s'", currentHw.c_str());
    }
    else
    {
        SWSS_LOG_THROW("number of switches is %zu in init view mode, this is not supported yet, FIXME", switches.size());
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

void onSyncdStart(bool warmStart)
{
    SWSS_LOG_ENTER();
    std::lock_guard<std::mutex> lock(g_mutex);

    /*
     * It may happen that after initialize we will receive some port
     * notifications with port'ids that are not in redis db yet, so after
     * checking VIDTORID map there will be entries and translate_vid_to_rid
     * will generate new id's for ports, this may cause race condition so we
     * need to use a lock here to prevent that.
     */

    SWSS_LOG_TIMER("on syncd start");

    if (warmStart)
    {
        /*
         * Switch was warm started, so switches map is empty, we need to
         * recreate it based on existing entries inside database.
         *
         * Currently we expect only one switch, then we need to call it.
         *
         * Also this will make sure that current switch id is the same as
         * before restart.
         *
         * If we want to support multiple switches, this needs to be adjusted.
         */

        performWarmRestart();

        SWSS_LOG_NOTICE("skipping hard reinit since WARM start was performed");
        return;
    }

    SWSS_LOG_NOTICE("performing hard reinit since COLD start was performed");

    /*
     * Switch was restarted in hard way, we need to perform hard reinit and
     * recreate switches map.
     */

    if (switches.size())
    {
        SWSS_LOG_THROW("performing hard reinit, but there are %zu switches defined, bug!", switches.size());
    }

    HardReiniter hr(g_vendorSai);

    hr.hardReinit();
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

    /*
     * NOTE: needs to be done per switch.
     */

    g_redisClient->del(VIDTORID);
}

void redisClearRidToVidMap()
{
    SWSS_LOG_ENTER();

    /*
     * NOTE: needs to be done per switch.
     */

    g_redisClient->del(RIDTOVID);
}


std::unordered_map<sai_object_id_t, sai_object_id_t> redisGetVidToRidMap()
{
    SWSS_LOG_ENTER();

    /*
     * NOTE: To support multiple switches VIDTORID must be per switch.
     */

    return SaiSwitch::redisGetObjectMap(VIDTORID);
}

std::unordered_map<sai_object_id_t, sai_object_id_t> redisGetRidToVidMap()
{
    SWSS_LOG_ENTER();

    /*
     * NOTE: To support multiple switches RIDTOVID must be per switch.
     */

    return SaiSwitch::redisGetObjectMap(RIDTOVID);
}


std::vector<std::string> redisGetAsicStateKeys()
{
    SWSS_LOG_ENTER();

    return g_redisClient->keys(ASIC_STATE_TABLE + std::string(":*"));
}


void checkWarmBootDiscoveredRids(
        _In_ std::shared_ptr<SaiSwitch> sw)
{
    SWSS_LOG_ENTER();

    /*
     * After switch was created, rid discovery method was called, and all
     * discovered RIDs should be present in current RID2VID map in redis
     * database. If any RID is missing, then ether there is bug in vendor code,
     * and after warm boot some RID values changed or we have a bug and forgot
     * to put rid/vid pair to redis.
     *
     * Assumption here is that during warm boot ASIC state will not change.
     */

    auto rid2vid = redisGetRidToVidMap();
    bool success = true;

    for (auto rid: sw->getDiscoveredRids())
    {
        if (rid2vid.find(rid) != rid2vid.end())
            continue;

        SWSS_LOG_ERROR("RID %s is missing from current RID2VID map after WARM boot!",
                sai_serialize_object_id(rid).c_str());

        success = false;
    }

    if (!success)
    {
        SWSS_LOG_THROW("FATAL, some discovered RIDs are not present in current RID2VID map, bug");
    }

    SWSS_LOG_NOTICE("all discovered RIDs are present in current RID2VID map");
}

void performWarmRestart()
{
    SWSS_LOG_ENTER();

    /*
     * There should be no case when we are doing warm restart and there is no
     * switch defined, we will throw at such a case.
     *
     * This case could be possible when no switches were created and only api
     * was initialized, but we will skip this scenario and address is when we
     * will have need for it.
     */

    auto entries = g_redisClient->keys(ASIC_STATE_TABLE + std::string(":SAI_OBJECT_TYPE_SWITCH:*"));

    if (entries.size() == 0)
    {
        SWSS_LOG_THROW("on warm restart there is no switches defined in DB, not supported yet, FIXME");
    }

    // TODO support multiple switches

    if (entries.size() != 1)
    {
        SWSS_LOG_THROW("multiple switches defined in warm start: %zu, not supported yet, FIXME", entries.size());
    }

    /*
     * Here we have only one switch defined, let's extract his vid and rid.
     */

    /*
     * Entry should be in format ASIC_STATE:SAI_OBJECT_TYPE_SWITCH:oid:0xYYYY
     *
     * Let's extract oid value
     */

    std::string key = entries.at(0);

    auto start = key.find_first_of(":") + 1;
    auto end = key.find(":", start);

    std::string strSwitchVid = key.substr(end + 1);

    sai_object_id_t switch_vid;

    sai_deserialize_object_id(strSwitchVid, switch_vid);

    sai_object_id_t orig_rid = g_translator->translateVidToRid(switch_vid);

    sai_object_id_t switch_rid;
    sai_attr_id_t   notifs[] = {
        SAI_SWITCH_ATTR_SWITCH_STATE_CHANGE_NOTIFY,
        SAI_SWITCH_ATTR_SHUTDOWN_REQUEST_NOTIFY,
        SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY,
        SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY,
        SAI_SWITCH_ATTR_QUEUE_PFC_DEADLOCK_NOTIFY
    };
#define NELMS(arr) (sizeof(arr) / sizeof(arr[0]))
    sai_attribute_t switch_attrs[NELMS(notifs) + 1];
    switch_attrs[0].id             = SAI_SWITCH_ATTR_INIT_SWITCH;
    switch_attrs[0].value.booldata = true;
    for (size_t i = 0; i < NELMS(notifs); i++)
    {
        switch_attrs[i+1].id        = notifs[i];
        switch_attrs[i+1].value.ptr = (void *)1; // any non-null pointer
    }
    g_handler->updateNotificationsPointers((uint32_t)NELMS(switch_attrs), &switch_attrs[0]);
    sai_status_t status;

    // TODO pass all non oid attributes needed for conditionals and for hardware info
    {
        SWSS_LOG_TIMER("Warm boot: create switch");
        status = g_vendorSai->create(SAI_OBJECT_TYPE_SWITCH, &switch_rid, 0, (uint32_t)NELMS(switch_attrs), &switch_attrs[0]);
    }

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_THROW("failed to create switch RID: %s",
                       sai_serialize_status(status).c_str());
    }
    if (orig_rid != switch_rid)
    {
        SWSS_LOG_THROW("Unexpected RID 0x%lx (expected 0x%lx)",
                       switch_rid, orig_rid);
    }

    /*
     * Perform all get operations on existing switch.
     */

    auto sw = switches[switch_vid] = std::make_shared<SaiSwitch>(switch_vid, switch_rid, true);

    /*
     * Populate gSwitchId since it's needed if we want to make multiple warm
     * starts in a row.
     */

//#ifdef SAITHRIFT

    if (gSwitchId != SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_THROW("gSwitchId already contain switch!, SAI THRIFT don't support multiple switches yet, FIXME");
    }

    gSwitchId = switch_rid; // TODO this is needed on warm boot
//#endif

    checkWarmBootDiscoveredRids(sw);

    startDiagShell(switch_rid);
}

/**
 * When set to true extra logging will be added for tracking references.  This
 * is useful for debugging, but for production operations this will produce too
 * much noise in logs, and we still can replay scenario using recordings.
 */
bool enableRefernceCountLogs = false;

// TODO for future we can have each switch in separate redis db index or even
// some switches in the same db index and some in separate.  Current redis get
// asic view is assuming all switches are in the same db index an also some
// operations per switch are accessing data base in SaiSwitch class.  This
// needs to be reorganised to access database per switch basis and get only
// data that corresponds to each particular switch and access correct db index.

std::map<sai_object_id_t, swss::TableDump> redisGetAsicView(
        _In_ const std::string &tableName)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_TIMER("get asic view from %s", tableName.c_str());

    swss::DBConnector db("ASIC_DB", 0);

    swss::Table table(&db, tableName);

    swss::TableDump dump;

    table.dump(dump);

    std::map<sai_object_id_t, swss::TableDump> map;

    for (auto& key: dump)
    {
        sai_object_meta_key_t mk;
        sai_deserialize_object_meta_key(key.first, mk);

        auto switchVID = VidManager::switchIdQuery(mk.objectkey.key.object_id);

        map[switchVID][key.first] = key.second;
    }

    SWSS_LOG_NOTICE("%s switch count: %zu:", tableName.c_str(), map.size());

    for (auto& kvp: map)
    {
        SWSS_LOG_NOTICE("%s: objects count: %zu",
                sai_serialize_object_id(kvp.first).c_str(),
                kvp.second.size());
    }

    return map;
}

void updateRedisDatabase(
    _In_ const std::vector<std::shared_ptr<AsicView>>& temporaryViews)
{
    SWSS_LOG_ENTER();

    // TODO: We can make LUA script for this which will be much faster.
    //
    // TODO: Needs to be revisited if ASIC views will be across multiple redis
    // database indexes.

    SWSS_LOG_TIMER("redis update");

    // Remove Asic State Table

    const auto &asicStateKeys = g_redisClient->keys(ASIC_STATE_TABLE + std::string(":*"));

    for (const auto &key: asicStateKeys)
    {
        g_redisClient->del(key);
    }

    // Remove Temp Asic State Table

    const auto &tempAsicStateKeys = g_redisClient->keys(TEMP_PREFIX ASIC_STATE_TABLE + std::string(":*"));

    for (const auto &key: tempAsicStateKeys)
    {
        g_redisClient->del(key);
    }

    // Save temporary views as current view in redis database.

    for (auto& tv: temporaryViews)
    {
        for (const auto &pair: tv->m_soAll)
        {
            const auto &obj = pair.second;

            const auto &attr = obj->getAllAttributes();

            std::string key = std::string(ASIC_STATE_TABLE) + ":" + obj->m_str_object_type + ":" + obj->m_str_object_id;

            SWSS_LOG_DEBUG("setting key %s", key.c_str());

            if (attr.size() == 0)
            {
                /*
                 * Object has no attributes, so populate using NULL just to
                 * indicate that object exists.
                 */

                g_redisClient->hset(key, "NULL", "NULL");
            }
            else
            {
                for (const auto &ap: attr)
                {
                    const auto saiAttr = ap.second;

                    g_redisClient->hset(key, saiAttr->getStrAttrId(), saiAttr->getStrAttrValue());
                }
            }
        }
    }

    /*
     * Remove previous RID2VID maps and apply new map.
     *
     * TODO: This needs to be done per switch, we can't remove all maps.
     */

    redisClearVidToRidMap();
    redisClearRidToVidMap();

    // TODO check if those 2 maps are consistent

    for (auto& tv: temporaryViews)
    {
        for (auto &kv: tv->m_ridToVid)
        {
            std::string strVid = sai_serialize_object_id(kv.second);
            std::string strRid = sai_serialize_object_id(kv.first);

            g_redisClient->hset(VIDTORID, strVid, strRid);
            g_redisClient->hset(RIDTOVID, strRid, strVid);
        }
    }

    SWSS_LOG_NOTICE("updated redis database");
}

extern std::shared_ptr<Syncd> g_syncd;

void dumpComparisonLogicOutput(
    _In_ const std::vector<std::shared_ptr<AsicView>>& currentViews)
{
    SWSS_LOG_ENTER();

    std::stringstream ss;

    size_t total = 0; // total operations from all switches

    for (auto& c: currentViews)
    {
        total += c->asicGetOperationsCount();
    }

    ss << "ASIC_OPERATIONS: " << total << std::endl;

    for (auto& c: currentViews)
    {
        ss << "ASIC_OPERATIONS on "
            << sai_serialize_object_id(c->getSwitchVid())
            << " : "
            << c->asicGetOperationsCount()
            << std::endl;

        for (const auto &op: c->asicGetWithOptimizedRemoveOperations())
        {
            const std::string &key = kfvKey(*op.m_op);
            const std::string &opp = kfvOp(*op.m_op);

            ss << "o " << opp << ": " << key << std::endl;

            const auto &values = kfvFieldsValues(*op.m_op);

            for (auto v: values)
                ss << "a: " << fvField(v) << " " << fvValue(v) << std::endl;
        }
    }

    std::ofstream log("applyview.log");

    if (log.is_open())
    {
        log << ss.str();

        log.close();

        SWSS_LOG_NOTICE("wrote apply_view asic operations to applyview.log");
    }
    else
    {
        SWSS_LOG_ERROR("failed to open applyview.log");
    }
}

sai_status_t syncdApplyView()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_TIMER("apply");

    /*
     * We assume that there will be no case that we will move from 1 to 0, also
     * if at the beginning there is no switch, then when user will send create,
     * and it will be actually created (real call) so there should be no case
     * when we are moving from 0 -> 1.
     */

    /*
     * This method contains 2 stages.
     *
     * First stage is non destructive, when orchagent will build new view, and
     * there will be bug in comparison logic in first stage, then syncd will
     * send failure when doing apply view to orchagent but it will still be
     * running. No asic operations are performed during this stage.
     *
     * Second stage is destructive, so if there will be bug in comparison logic
     * or any asic operation will fail, then syncd will crash, since asic will
     * be in inconsistent state.
     */

    /*
     * Initialize rand for future candidate object selection if necessary.
     *
     * NOTE: Should this be deterministic? So we could repeat random choice
     * when something bad happen or we hit a bug, so in that case it will be
     * easier for reproduce, we could at least log value returned from time().
     *
     * TODO: To make it stable, we also need to make stable redisGetAsicView
     * since now order of items is random. Also redis result needs to be
     * sorted.
     */

    // Read current and temporary views from REDIS.

    auto currentMap = redisGetAsicView(ASIC_STATE_TABLE);
    auto temporaryMap = redisGetAsicView(TEMP_PREFIX ASIC_STATE_TABLE);

    if (currentMap.size() != temporaryMap.size())
    {
        SWSS_LOG_THROW("current view switches: %zu != temporary view switches: %zu, FATAL",
                currentMap.size(),
                temporaryMap.size());
    }

    if (currentMap.size() != switches.size())
    {
        SWSS_LOG_THROW("current asic view switches %zu != defined switches %zu, FATAL",
                currentMap.size(),
                switches.size());
    }

    // VID of switches must match for each map

    for (auto& kvp: currentMap)
    {
        if (temporaryMap.find(kvp.first) == temporaryMap.end())
        {
            SWSS_LOG_THROW("switch VID %s missing from temporary view!, FATAL",
                    sai_serialize_object_id(kvp.first).c_str());
        }

        if (switches.find(kvp.first) == switches.end())
        {
            SWSS_LOG_THROW("switch VID %s missing from ASIC, FATAL",
                    sai_serialize_object_id(kvp.first).c_str());
        }
    }

    std::vector<std::shared_ptr<AsicView>> currentViews;
    std::vector<std::shared_ptr<AsicView>> tempViews;
    std::vector<std::shared_ptr<ComparisonLogic>> cls;

    try
    {
        for (auto& kvp: switches)
        {
            auto switchVid = kvp.first;

            auto sw = switches.at(switchVid);

            /*
             * We are starting first stage here, it still can throw exceptions
             * but it's non destructive for ASIC, so just catch and return in
             * case of failure.
             *
             * Each ASIC view at this point will contain only 1 switch.
             */

            auto current = std::make_shared<AsicView>(currentMap.at(switchVid));
            auto temp = std::make_shared<AsicView>(temporaryMap.at(switchVid));

            auto cl = std::make_shared<ComparisonLogic>(g_vendorSai, sw, g_syncd->m_initViewRemovedVidSet, current, temp);

            cl->compareViews();

            currentViews.push_back(current);
            tempViews.push_back(temp);
            cls.push_back(cl);
        }
    }
    catch (const std::exception &e)
    {
        /*
         * Exception was thrown in first stage, those were non destructive
         * actions so just log exception and let syncd running.
         */

        SWSS_LOG_ERROR("Exception: %s", e.what());

        return SAI_STATUS_FAILURE;
    }

    /*
     * This is second stage. Those operations are destructive, if any of them
     * fail, then we will have inconsistent state in ASIC.
     */

    if (g_commandLineOptions->m_enableUnittests)
    {
        dumpComparisonLogicOutput(currentViews);
    }

    for (auto& cl: cls)
    {
        cl->executeOperationsOnAsic(); // can throw, if so asic will be in inconsistent state
    }

    updateRedisDatabase(tempViews);

    for (auto& cl: cls)
    {
        if (g_commandLineOptions->m_enableConsistencyCheck)
        {
            bool consistent = cl->checkAsicVsDatabaseConsistency();

            if (!consistent && g_commandLineOptions->m_enableUnittests)
            {
                SWSS_LOG_THROW("ASIC content is differnt than DB content!");
            }
        }
    }

    return SAI_STATUS_SUCCESS;
}

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

    std::string fdbFlushLuaScript = swss::loadLuaScript(fdbFlushLuaScriptName);
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
        onSyncdStart(g_commandLineOptions->m_startType == SAI_START_TYPE_WARM_BOOT);
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
