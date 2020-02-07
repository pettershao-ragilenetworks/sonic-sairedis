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

#include <inttypes.h>
#include <limits.h>

#include <iostream>
#include <map>
#include <unordered_map>

#define DEF_SAI_WARM_BOOT_DATA_FILE "/var/warmboot/sai-warmboot.bin"
#define MAX_OBJLIST_LEN 128

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
std::shared_ptr<swss::ProducerTable>        getResponse;
std::shared_ptr<swss::NotificationProducer> notifications;

/*
 * TODO: Those are hard coded values for mlnx integration for v1.0.1 they need
 * to be updated.
 *
 * Also DEVICE_MAC_ADDRESS is not present in saiswitch.h
 */
std::map<std::string, std::string> gProfileMap;

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

/**
 * @brief set of objects removed by user when we are in init view mode. Those
 * could be vlan members, bridge ports etc.
 *
 * We need this list to later on not put them back to temp view mode when doing
 * populate existing objects in apply view mode.
 *
 * Object ids here a VIDs.
 */
std::set<sai_object_id_t> initViewRemovedVidSet;


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

void snoop_get_attr(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ const std::string &attr_id,
        _In_ const std::string &attr_value)
{
    SWSS_LOG_ENTER();

    /*
     * Note: str_object_type + ":" + str_object_id is meta_key we can us that
     * here later on.
     */

    std::string str_object_type = sai_serialize_object_type(object_type);

    std::string prefix = "";

    if (g_syncd->isInitViewMode())
    {
        prefix = TEMP_PREFIX;
    }

    std::string key = prefix + (ASIC_STATE_TABLE + (":" + str_object_type + ":" + str_object_id));

    SWSS_LOG_DEBUG("%s", key.c_str());


    g_redisClient->hset(key, attr_id, attr_value);
}

void snoop_get_oid(
        _In_ sai_object_id_t vid)
{
    SWSS_LOG_ENTER();

    if (vid == SAI_NULL_OBJECT_ID)
    {
        /*
         * If snooped oid is NULL then we don't need take any action.
         */

        return;
    }

    /*
     * We need use redis version of object type query here since we are
     * operating on VID value, and syncd is compiled against real SAI
     * implementation which has different function g_vendorSai->objectTypeQuery.
     */

    sai_object_type_t object_type = VidManager::objectTypeQuery(vid);

    std::string str_vid = sai_serialize_object_id(vid);

    snoop_get_attr(object_type, str_vid, "NULL", "NULL");
}

void snoop_get_oid_list(
        _In_ const sai_object_list_t &list)
{
    SWSS_LOG_ENTER();

    for (uint32_t i = 0; i < list.count; i++)
    {
        snoop_get_oid(list.list[i]);
    }
}

void snoop_get_attr_value(
        _In_ const std::string &str_object_id,
        _In_ const sai_attr_metadata_t *meta,
        _In_ const sai_attribute_t &attr)
{
    SWSS_LOG_ENTER();

    std::string value = sai_serialize_attr_value(*meta, attr);

    SWSS_LOG_DEBUG("%s:%s", meta->attridname, value.c_str());

    snoop_get_attr(meta->objecttype, str_object_id, meta->attridname, value);
}

void snoop_get_response(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    /*
     * NOTE: this method is operating on VIDs, all RIDs were translated outside
     * this method.
     */

    /*
     * Vlan (including vlan 1) will need to be put into TEMP view this should
     * also be valid for all objects that were queried.
     */

    for (uint32_t idx = 0; idx < attr_count; ++idx)
    {
        const sai_attribute_t &attr = attr_list[idx];

        auto meta = sai_metadata_get_attr_metadata(object_type, attr.id);

        if (meta == NULL)
        {
            SWSS_LOG_THROW("unable to get metadata for object type %d, attribute %d", object_type, attr.id);
        }

        /*
         * We should snoop oid values even if they are readonly we just note in
         * temp view that those objects exist on switch.
         */

        switch (meta->attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:
                snoop_get_oid(attr.value.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:
                snoop_get_oid_list(attr.value.objlist);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:
                if (attr.value.aclfield.enable)
                    snoop_get_oid(attr.value.aclfield.data.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:
                if (attr.value.aclfield.enable)
                    snoop_get_oid_list(attr.value.aclfield.data.objlist);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:
                if (attr.value.aclaction.enable)
                    snoop_get_oid(attr.value.aclaction.parameter.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:
                if (attr.value.aclaction.enable)
                    snoop_get_oid_list(attr.value.aclaction.parameter.objlist);
                break;

            default:

                /*
                 * If in future new attribute with object id will be added this
                 * will make sure that we will need to add handler here.
                 */

                if (meta->isoidattribute)
                {
                    SWSS_LOG_THROW("attribute %s is object id, but not processed, FIXME", meta->attridname);
                }

                break;
        }

        if (SAI_HAS_FLAG_READ_ONLY(meta->flags))
        {
            /*
             * If value is read only, we skip it, since after syncd restart we
             * won't be able to set/create it anyway.
             */

            continue;
        }

        if (meta->objecttype == SAI_OBJECT_TYPE_PORT &&
                meta->attrid == SAI_PORT_ATTR_HW_LANE_LIST)
        {
            /*
             * Skip port lanes for now since we don't create ports.
             */

            SWSS_LOG_INFO("skipping %s for %s", meta->attridname, str_object_id.c_str());
            continue;
        }

        /*
         * Put non readonly, and non oid attribute value to temp view.
         *
         * NOTE: This will also put create-only attributes to view, and after
         * syncd hard reinit we will not be able to do "SET" on that attribute.
         *
         * Similar action can happen when we will do this on asicSet during
         * apply view.
         */

        snoop_get_attr_value(str_object_id, meta, attr);
    }
}

void internal_syncd_get_send(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ sai_object_id_t switch_id,
        _In_ sai_status_t status,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> entry;

    if (status == SAI_STATUS_SUCCESS)
    {
        g_translator->translateRidToVid(object_type, switch_id, attr_count, attr_list);

        /*
         * Normal serialization + translate RID to VID.
         */

        entry = SaiAttributeList::serialize_attr_list(
                object_type,
                attr_count,
                attr_list,
                false);

        /*
         * All oid values here are VIDs.
         */

        snoop_get_response(object_type, str_object_id, attr_count, attr_list);
    }
    else if (status == SAI_STATUS_BUFFER_OVERFLOW)
    {
        /*
         * In this case we got correct values for list, but list was too small
         * so serialize only count without list itself, sairedis will need to
         * take this into account when deserialize.
         *
         * If there was a list somewhere, count will be changed to actual value
         * different attributes can have different lists, many of them may
         * serialize only count, and will need to support that on the receiver.
         */

        entry = SaiAttributeList::serialize_attr_list(
                object_type,
                attr_count,
                attr_list,
                true);
    }
    else
    {
        /*
         * Some other error, don't send attributes at all.
         */
    }

    for (const auto &e: entry)
    {
        SWSS_LOG_DEBUG("attr: %s: %s", fvField(e).c_str(), fvValue(e).c_str());
    }

    std::string str_status = sai_serialize_status(status);

    SWSS_LOG_INFO("sending response for GET api with status: %s", str_status.c_str());

    /*
     * Since we have only one get at a time, we don't have to serialize object
     * type and object id, only get status is required to be returned.  Get
     * response will not put any data to table, only queue is used.
     */

    getResponse->set(str_status, entry, REDIS_ASIC_STATE_COMMAND_GETRESPONSE);

    SWSS_LOG_INFO("response for GET api was send");
}

/**
 * @brief Send api response.
 *
 * This function should be use to send response to sairedis for
 * create/remove/set API as well as their corresponding bulk versions.
 *
 * Should not be used on GET api.
 */
void sendApiResponse(
        _In_ sai_common_api_t api,
        _In_ sai_status_t status,
        _In_ uint32_t object_count = 0,
        _In_ sai_status_t * object_statuses = NULL)
{
    SWSS_LOG_ENTER();

    /*
     * By default synchronous mode is disabled and can be enabled by command
     * line on syncd start. This will also require to enable synchronous mode
     * in OA/sairedis because same GET RESPONSE channel is used to generate
     * response for sairedis quad API.
     */

    if (!g_commandLineOptions->m_enableSyncMode)
    {
        return;
    }

    switch (api)
    {
        case SAI_COMMON_API_CREATE:
        case SAI_COMMON_API_REMOVE:
        case SAI_COMMON_API_SET:
        case SAI_COMMON_API_BULK_CREATE:
        case SAI_COMMON_API_BULK_REMOVE:
        case SAI_COMMON_API_BULK_SET:
            break;

        default:
            SWSS_LOG_THROW("api %s not supported by this function",
                    sai_serialize_common_api(api).c_str());
    }

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_THROW("api %s failed in syncd mode: %s, but entry STILL exists in REDIS db and must be removed, FIXME",
                    sai_serialize_common_api(api).c_str(),
                    sai_serialize_status(status).c_str());
    }

    std::vector<swss::FieldValueTuple> entry;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        swss::FieldValueTuple fvt(sai_serialize_status(object_statuses[idx]), "");

        entry.push_back(fvt);
    }

    std::string str_status = sai_serialize_status(status);

    SWSS_LOG_INFO("sending response for %d api with status: %s", api, str_status.c_str());

    getResponse->set(str_status, entry, REDIS_ASIC_STATE_COMMAND_GETRESPONSE);

    SWSS_LOG_INFO("response for %d api was send", api);
}

const char* profile_get_value(
        _In_ sai_switch_profile_id_t profile_id,
        _In_ const char* variable)
{
    SWSS_LOG_ENTER();

    if (variable == NULL)
    {
        SWSS_LOG_WARN("variable is null");
        return NULL;
    }

    auto it = gProfileMap.find(variable);

    if (it == gProfileMap.end())
    {
        SWSS_LOG_NOTICE("%s: NULL", variable);
        return NULL;
    }

    SWSS_LOG_NOTICE("%s: %s", variable, it->second.c_str());

    return it->second.c_str();
}

std::map<std::string, std::string>::iterator gProfileIter = gProfileMap.begin();

int profile_get_next_value(
        _In_ sai_switch_profile_id_t profile_id,
        _Out_ const char** variable,
        _Out_ const char** value)
{
    SWSS_LOG_ENTER();

    if (value == NULL)
    {
        SWSS_LOG_INFO("resetting profile map iterator");

        gProfileIter = gProfileMap.begin();
        return 0;
    }

    if (variable == NULL)
    {
        SWSS_LOG_WARN("variable is null");
        return -1;
    }

    if (gProfileIter == gProfileMap.end())
    {
        SWSS_LOG_INFO("iterator reached end");
        return -1;
    }

    *variable = gProfileIter->first.c_str();
    *value = gProfileIter->second.c_str();

    SWSS_LOG_INFO("key: %s:%s", *variable, *value);

    gProfileIter++;

    return 0;
}

void startDiagShell()
{
    SWSS_LOG_ENTER();

    if (g_commandLineOptions->m_enableDiagShell)
    {
        SWSS_LOG_NOTICE("starting diag shell thread");

        /*
         * TODO actual switch id must be supplied
         */

        std::thread diag_shell_thread = std::thread(sai_diag_shell, SAI_NULL_OBJECT_ID);

        diag_shell_thread.detach();
    }
}

void on_switch_create(
        _In_ sai_object_id_t switch_vid)
{
    SWSS_LOG_ENTER();

    sai_object_id_t switch_rid = g_translator->translateVidToRid(switch_vid);

    if (switches.size() > 0)
    {
        SWSS_LOG_THROW("creating multiple switches is not supported yet, FIXME");
    }

    /*
     * All needed data to populate switch should be obtained inside SaiSwitch
     * constructor, like getting all queues, ports, etc.
     */

    switches[switch_vid] = std::make_shared<SaiSwitch>(switch_vid, switch_rid);

    startDiagShell();
}

void on_switch_remove(
        _In_ sai_object_id_t switch_id_vid)
    __attribute__ ((__noreturn__));

void on_switch_remove(
        _In_ sai_object_id_t switch_id_vid)
{
    SWSS_LOG_ENTER();

    /*
     * On remove switch there should be extra action all local objects and
     * redis object should be removed on remove switch local and redis db
     * objects should be cleared.
     *
     * Currently we don't want to remove switch so we don't need this method,
     * but lets put this as a safety check.
     *
     * To support multiple switches this function needs to be refactored.
     */

    SWSS_LOG_THROW("remove switch is not implemented, FIXME");
}

/**
 * @brief Determines whether attribute is "workaround" attribute for SET API.
 *
 * Some attributes are not supported on SET API on different platforms.
 * For example SAI_SWITCH_ATTR_SRC_MAC_ADDRESS.
 *
 * @param[in] objecttype Object type.
 * @param[in] attrid Attribute Id.
 * @param[in] status Status from SET API.
 *
 * @return True if error from SET API can be ignored, false otherwise.
 */
bool is_set_attribute_workaround(
        _In_ sai_object_type_t objecttype,
        _In_ sai_attr_id_t attrid,
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    if (status == SAI_STATUS_SUCCESS)
    {
        return false;
    }

    if (objecttype == SAI_OBJECT_TYPE_SWITCH &&
            attrid == SAI_SWITCH_ATTR_SRC_MAC_ADDRESS)
    {
        SWSS_LOG_WARN("setting %s failed: %s, not all platforms support this attribute",
                sai_metadata_get_attr_metadata(objecttype, attrid)->attridname,
                sai_serialize_status(status).c_str());

        return true;
    }

    return false;
}

void get_port_related_objects(
        _In_ sai_object_id_t port_rid,
        _Out_ std::vector<sai_object_id_t>& related)
{
    SWSS_LOG_ENTER();

    related.clear();

    sai_object_meta_key_t meta_key;

    meta_key.objecttype = SAI_OBJECT_TYPE_PORT;
    meta_key.objectkey.key.object_id = port_rid;

    sai_attr_id_t attrs[3] = {
        SAI_PORT_ATTR_QOS_QUEUE_LIST,
        SAI_PORT_ATTR_QOS_SCHEDULER_GROUP_LIST,
        SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST
    };

    for (size_t i = 0; i < sizeof(attrs)/sizeof(sai_attr_id_t); i++)
    {
        std::vector<sai_object_id_t> objlist;

        objlist.resize(MAX_OBJLIST_LEN);

        sai_attribute_t attr;

        attr.id = attrs[i];

        attr.value.objlist.count = MAX_OBJLIST_LEN;
        attr.value.objlist.list = objlist.data();

        auto status = g_vendorSai->get(meta_key, 1, &attr);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_THROW("failed to obtain related obejcts for port rid %s: %s, attr id: %d",
                    sai_serialize_object_id(port_rid).c_str(),
                    sai_serialize_status(status).c_str(),
                    attr.id);
        }

        objlist.resize(attr.value.objlist.count);

        related.insert(related.end(), objlist.begin(), objlist.end());
    }

    SWSS_LOG_NOTICE("obtained %zu port %s related RIDs",
            related.size(),
            sai_serialize_object_id(port_rid).c_str());
}

void post_port_remove(
        _In_ std::shared_ptr<SaiSwitch> sw,
        _In_ sai_object_id_t port_rid,
        _In_ const std::vector<sai_object_id_t>& relatedRids)
{
    SWSS_LOG_ENTER();

    /*
     * Port was successfully removed from vendor SAI,
     * we need to remove queues, ipgs and sg from:
     *
     * - redis ASIC DB
     * - discovered existing objects in saiswitch class
     * - local vid2rid map
     * - redis RIDTOVID map
     *
     * also remove LANES mapping
     */

    for (auto rid: relatedRids)
    {
        // remove from existing objects

        if (sw->isDiscoveredRid(rid))
        {
            sw->removeExistingObjectReference(rid);
        }

        // remove from RID2VID and VID2RID map in redis

        std::string str_rid = sai_serialize_object_id(rid);

        auto pvid = g_redisClient->hget(RIDTOVID, str_rid);

        if (pvid == nullptr)
        {
            SWSS_LOG_THROW("expected rid %s to be present in RIDTOVID", str_rid.c_str());
        }

        std::string str_vid = *pvid;

        sai_object_id_t vid;
        sai_deserialize_object_id(str_vid, vid);

        // TODO should this remove rid,vid and object be as db op?

        g_translator->eraseRidAndVid(rid, vid);

        // remove from ASIC DB

        sai_object_type_t ot = VidManager::objectTypeQuery(vid);

        std::string key = ASIC_STATE_TABLE + std::string(":") + sai_serialize_object_type(ot) + ":" + str_vid;

        SWSS_LOG_INFO("removing ASIC DB key: %s", key.c_str());

        g_redisClient->del(key);
    }

    sw->onPostPortRemove(port_rid);

    SWSS_LOG_NOTICE("post port remove actions succeeded");
}

void post_port_create(
        _In_ std::shared_ptr<SaiSwitch> sw,
        _In_ sai_object_id_t port_rid,
        _In_ sai_object_id_t port_vid)
{
    SWSS_LOG_ENTER();

    sw->onPostPortCreate(port_rid, port_vid);

    SWSS_LOG_NOTICE("post port create actions succeeded");
}

sai_status_t genericCreate(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    sai_object_id_t object_id;
    sai_deserialize_object_id(str_object_id, object_id);

    sai_object_meta_key_t meta_key;

    meta_key.objecttype = object_type;
    meta_key.objectkey.key.object_id = object_id;


    // Object id is VID, we can use it to extract switch id.

    sai_object_id_t switch_id = VidManager::switchIdQuery(object_id);

    if (switch_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_THROW("invalid switch_id translated from VID 0x%" PRIx64, object_id);
    }

    if (object_type != SAI_OBJECT_TYPE_SWITCH)
    {
        /*
         * When we creating switch, then switch_id parameter is
         * ignored, but we can't convert it using vid to rid map,
         * since rid doesn't exist yet, so skip translate for switch,
         * but use translate for all other objects.
         */

        switch_id = g_translator->translateVidToRid(switch_id);
    }
    else
    {
        if (switches.size() > 0)
        {
            /*
             * NOTE: to support multiple switches we need support
             * here for create.
             */

            SWSS_LOG_THROW("creating multiple switches is not supported yet, FIXME");
        }
    }

    if (object_type == SAI_OBJECT_TYPE_PORT)
    {
        if (g_syncd->isInitViewMode())
        {
            // reason for this is that if user will create port,
            // new port is not actually created so when for example
            // querying new queues for new created port, there are
            // not there, since no actual port create was issued on
            // the ASIC
            SWSS_LOG_THROW("port object can't be created in init view mode");
        }
    }

    sai_status_t status = g_vendorSai->create(meta_key, switch_id, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        sai_object_id_t real_object_id = meta_key.objectkey.key.object_id;

        /*
         * Object was created so new object id was generated we
         * need to save virtual id's to redis db.
         */

        std::string str_vid = sai_serialize_object_id(object_id);
        std::string str_rid = sai_serialize_object_id(real_object_id);

        g_translator->insertRidAndVid(real_object_id, object_id);

        SWSS_LOG_INFO("saved VID %s to RID %s", str_vid.c_str(), str_rid.c_str());

        if (object_type == SAI_OBJECT_TYPE_SWITCH)
        {
            on_switch_create(switch_id);
            gSwitchId = real_object_id;
            SWSS_LOG_NOTICE("Initialize gSwitchId with ID = 0x%" PRIx64, gSwitchId);
        }

        if (object_type == SAI_OBJECT_TYPE_PORT)
        {
            sai_object_id_t switch_vid = VidManager::switchIdQuery(object_id);

            post_port_create(switches.at(switch_vid), real_object_id, object_id);
        }
    }

    return status;
}

sai_status_t genericRemove(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id)
{
    SWSS_LOG_ENTER();

    sai_object_id_t object_id;
    sai_deserialize_object_id(str_object_id, object_id);

    sai_object_meta_key_t meta_key;

    meta_key.objecttype = object_type;
    meta_key.objectkey.key.object_id = object_id;

    sai_object_id_t rid = g_translator->translateVidToRid(object_id);

    meta_key.objectkey.key.object_id = rid;

    std::vector<sai_object_id_t> related;

    if (object_type == SAI_OBJECT_TYPE_PORT)
    {
        if (g_syncd->isInitViewMode())
        {
            // reason for this is that if user will remove port,
            // and the create new one in init view mode, then this
            // new port is not actually created so when for example
            // querying new queues for new created port, there are
            // not there, since no actual port create was issued on
            // the ASIC
            SWSS_LOG_THROW("port object can't be removed in init view mode");
        }

        // collect queues, ipgs, sg that belong to port
        get_port_related_objects(rid, related);
    }

    sai_status_t status = g_vendorSai->remove(meta_key);

    if (status == SAI_STATUS_SUCCESS)
    {
        std::string str_vid = sai_serialize_object_id(object_id);
        std::string str_rid = sai_serialize_object_id(rid);

        g_translator->eraseRidAndVid(rid, object_id);

        // TODO remove all related objects from REDIS DB and also
        // from existing object references since at this point
        // they are no longer valid

        if (object_type == SAI_OBJECT_TYPE_SWITCH)
        {
            on_switch_remove(object_id);
        }
        else
        {
            /*
             * Removing some object succeeded. Let's check if that
             * object was default created object, eg. vlan member.
             * Then we need to update default created object map in
             * SaiSwitch to be in sync, and be prepared for apply
             * view to transfer those synced default created
             * objects to temporary view when it will be created,
             * since that will be out basic switch state.
             *
             * TODO: there can be some issues with reference count
             * like for schedulers on scheduler groups since they
             * should have internal references, and we still need
             * to create dependency tree from saiDiscovery and
             * update those references to track them, this is
             * printed in metadata sanitycheck as "default value
             * needs to be stored".
             *
             * TODO lets add SAI metadata flag for that this will
             * also needs to be of internal/vendor default but we
             * can already deduce that.
             */

            sai_object_id_t switch_vid = VidManager::switchIdQuery(object_id);

            if (switches.at(switch_vid)->isDiscoveredRid(rid))
            {
                switches.at(switch_vid)->removeExistingObjectReference(rid);
            }

            if (object_type == SAI_OBJECT_TYPE_PORT)
            {
                post_port_remove(switches.at(switch_vid), rid, related);
            }
        }
    }

    return status;
}

sai_status_t genericSet(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    sai_object_id_t object_id;
    sai_deserialize_object_id(str_object_id, object_id);

    sai_object_meta_key_t meta_key;

    meta_key.objecttype = object_type;
    meta_key.objectkey.key.object_id = object_id;

    sai_object_id_t rid = g_translator->translateVidToRid(object_id);

    meta_key.objectkey.key.object_id = rid;

    sai_status_t status = g_vendorSai->set(meta_key, attr);

    if (is_set_attribute_workaround(meta_key.objecttype, attr->id, status))
    {
        return SAI_STATUS_SUCCESS;
    }

    return status;
}

sai_status_t genericGet(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    sai_object_id_t object_id;
    sai_deserialize_object_id(str_object_id, object_id);

    sai_object_meta_key_t meta_key;

    meta_key.objecttype = object_type;
    meta_key.objectkey.key.object_id = object_id;

    sai_object_id_t rid = g_translator->translateVidToRid(object_id);

    meta_key.objectkey.key.object_id = rid;

    return g_vendorSai->get(meta_key, attr_count, attr_list);
}

sai_status_t processOid(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ sai_common_api_t api,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    sai_object_id_t object_id;
    sai_deserialize_object_id(str_object_id, object_id);

    SWSS_LOG_DEBUG("calling %s for %s",
            sai_serialize_common_api(api).c_str(),
            sai_serialize_object_type(object_type).c_str());

//    sai_object_meta_key_t meta_key;
//
//    meta_key.objecttype = object_type;
//    meta_key.objectkey.key.object_id = object_id;

    /*
     * We need to do translate vid/rid except for create, since create will
     * create new RID value, and we will have to map them to VID we received in
     * create query.
     */

    auto info = sai_metadata_get_object_type_info(object_type);

    if (info->isnonobjectid)
    {
        SWSS_LOG_THROW("passing non object id %s as generic object", info->objecttypename);
    }

    switch (api)
    {
        case SAI_COMMON_API_CREATE:
            return genericCreate(object_type, str_object_id, attr_count, attr_list);

        case SAI_COMMON_API_REMOVE:
            return genericRemove(object_type, str_object_id);

        case SAI_COMMON_API_SET:
            return genericSet(object_type, str_object_id, attr_list);

        case SAI_COMMON_API_GET:
            return genericGet(object_type, str_object_id, attr_count, attr_list);

        default:

            SWSS_LOG_THROW("common api (%s) is not implemented", sai_serialize_common_api(api).c_str());
    }
}

void sendNotifyResponse(
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    std::string str_status = sai_serialize_status(status);

    std::vector<swss::FieldValueTuple> entry;

    SWSS_LOG_INFO("sending response: %s", str_status.c_str());

    getResponse->set(str_status, entry, "notify");
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

    initViewRemovedVidSet.clear();
}

void InspectAsic()
{
    SWSS_LOG_ENTER();

    // Fetch all the keys from ASIC DB
    // Loop through all the keys in ASIC DB
    std::string pattern = ASIC_STATE_TABLE + std::string(":*");
    for (const auto &key: g_redisClient->keys(pattern))
    {
        // ASIC_STATE:objecttype:objectid (object id may contain ':')
        auto start = key.find_first_of(":");
        if (start == std::string::npos)
        {
            SWSS_LOG_ERROR("invalid ASIC_STATE_TABLE %s: no start :", key.c_str());
            break;
        }
        auto mid = key.find_first_of(":", start + 1);
        if (mid == std::string::npos)
        {
            SWSS_LOG_ERROR("invalid ASIC_STATE_TABLE %s: no mid :", key.c_str());
            break;
        }
        auto str_object_type = key.substr(start + 1, mid - start - 1);
        auto str_object_id  = key.substr(mid + 1);

        sai_object_type_t object_type;
        sai_deserialize_object_type(str_object_type, object_type);

        // Find all the attrid from ASIC DB, and use them to query ASIC
        auto hash = g_redisClient->hgetall(key);
        std::vector<swss::FieldValueTuple> values;
        for (auto &kv: hash)
        {
            const std::string &skey = kv.first;
            const std::string &svalue = kv.second;

            swss::FieldValueTuple fvt(skey, svalue);

            values.push_back(fvt);
        }

        SaiAttributeList list(object_type, values, false);

        sai_attribute_t *attr_list = list.get_attr_list();

        uint32_t attr_count = list.get_attr_count();

        SWSS_LOG_DEBUG("attr count: %u", list.get_attr_count());

        if (attr_count == 0)
        {
            // TODO: how to check ASIC on ASIC DB key with NULL:NULL hash
            continue; // Just ignore
        }

        auto info = sai_metadata_get_object_type_info(object_type);

        // Call SAI Get API on this key
        sai_status_t status;
        switch (object_type)
        {
            case SAI_OBJECT_TYPE_FDB_ENTRY:
            {
                sai_fdb_entry_t fdb_entry;
                sai_deserialize_fdb_entry(str_object_id, fdb_entry);

                // TODO make those translation generic iterating members !

                fdb_entry.switch_id = g_translator->translateVidToRid(fdb_entry.switch_id);
                fdb_entry.bv_id = g_translator->translateVidToRid(fdb_entry.bv_id);

                status = g_vendorSai->get(&fdb_entry, attr_count, attr_list);
                break;
            }

            case SAI_OBJECT_TYPE_NEIGHBOR_ENTRY:
            {
                sai_neighbor_entry_t neighbor_entry;
                sai_deserialize_neighbor_entry(str_object_id, neighbor_entry);

                neighbor_entry.switch_id = g_translator->translateVidToRid(neighbor_entry.switch_id);
                neighbor_entry.rif_id = g_translator->translateVidToRid(neighbor_entry.rif_id);

                status = g_vendorSai->get(&neighbor_entry, attr_count, attr_list);
                break;
            }

            case SAI_OBJECT_TYPE_ROUTE_ENTRY:
            {
                sai_route_entry_t route_entry;
                sai_deserialize_route_entry(str_object_id, route_entry);

                route_entry.switch_id = g_translator->translateVidToRid(route_entry.switch_id);
                route_entry.vr_id = g_translator->translateVidToRid(route_entry.vr_id);

                status = g_vendorSai->get(&route_entry, attr_count, attr_list);
                break;
            }

            case SAI_OBJECT_TYPE_NAT_ENTRY:
            {
                sai_nat_entry_t nat_entry;
                sai_deserialize_nat_entry(str_object_id, nat_entry);

                nat_entry.switch_id = g_translator->translateVidToRid(nat_entry.switch_id);
                nat_entry.vr_id = g_translator->translateVidToRid(nat_entry.vr_id);

                status = g_vendorSai->get(&nat_entry, attr_count, attr_list);
                break;
            }

            default:
            {
                if (info->isnonobjectid)
                {
                    SWSS_LOG_THROW("object %s:%s is non object id, but not handled, FIXME",
                            sai_serialize_object_type(object_type).c_str(),
                            str_object_id.c_str());
                }

                sai_object_id_t object_id;
                sai_deserialize_object_id(str_object_id, object_id);

                sai_object_meta_key_t meta_key;

                meta_key.objecttype = object_type;
                meta_key.objectkey.key.object_id = g_translator->translateVidToRid(object_id);

                status = g_vendorSai->get(meta_key, attr_count, attr_list);
                break;
            }
        }

        if (status == SAI_STATUS_NOT_IMPLEMENTED)
        {
            SWSS_LOG_ERROR("not implemented get api: %s", str_object_type.c_str());
        }
        else if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("failed to execute get api: %s", sai_serialize_status(status).c_str());
        }

        // Compare fields and values from ASIC_DB and SAI response
        // Log the difference
        for (uint32_t index = 0; index < attr_count; ++index)
        {
            const sai_attribute_t *attr = &attr_list[index];

            auto meta = sai_metadata_get_attr_metadata(object_type, attr->id);

            if (meta == NULL)
            {
                SWSS_LOG_ERROR("FATAL: failed to find metadata for object type %d and attr id %d", object_type, attr->id);
                break;
            }

            std::string str_attr_id = sai_serialize_attr_id(*meta);

            std::string str_attr_value = sai_serialize_attr_value(*meta, *attr, false);

            std::string hash_attr_value = hash[str_attr_id];
            if (hash_attr_value == str_attr_value)
            {
                SWSS_LOG_INFO("Matched %s redis attr %s with asic attr %s for %s:%s", str_attr_id.c_str(), hash_attr_value.c_str(), str_attr_value.c_str(), str_object_type.c_str(), str_object_id.c_str());
            }
            else
            {
                SWSS_LOG_ERROR("Failed to match %s redis attr %s with asic attr %s for %s:%s", str_attr_id.c_str(), hash_attr_value.c_str(), str_attr_value.c_str(), str_object_type.c_str(), str_object_id.c_str());
            }
        }
    }
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

    static bool firstInitWasPerformed = false;

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

        InspectAsic();

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

sai_status_t initViewCreate(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    if (object_type == SAI_OBJECT_TYPE_PORT)
    {
        // reason for this is that if user will create port,
        // new port is not actually created so when for example
        // querying new queues for new created port, there are
        // not there, since no actual port create was issued on
        // the ASIC
        SWSS_LOG_THROW("port object can't be created in init view mode");
    }

    auto info = sai_metadata_get_object_type_info(object_type);

    // we assume create of those non object id object types will succeed

    if (info->isobjectid)
    {
        sai_object_id_t object_id;
        sai_deserialize_object_id(str_object_id, object_id);

        /*
         * Object ID here is actual VID returned from redis during
         * creation this is floating VID in init view mode.
         */

        SWSS_LOG_DEBUG("generic create (init view) for %s, floating VID: %s",
                sai_serialize_object_type(object_type).c_str(),
                sai_serialize_object_id(object_id).c_str());

        if (object_type == SAI_OBJECT_TYPE_SWITCH)
        {
            on_switch_create_in_init_view(object_id, attr_count, attr_list);
        }
    }

    sendApiResponse(SAI_COMMON_API_CREATE, SAI_STATUS_SUCCESS);

    return SAI_STATUS_SUCCESS;
}

sai_status_t initViewRemove(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id)
{
    SWSS_LOG_ENTER();

    if (object_type == SAI_OBJECT_TYPE_PORT)
    {
        // reason for this is that if user will remove port, actual
        // resources for it wont be release, lanes would be still
        // occupied and there is extra logic required in post port
        // remove which clears OIDs (ipgs,queues,SGs) from redis db
        // that are automatically removed by vendor SAI, and comparison
        // logic don't support that
        SWSS_LOG_THROW("port object can't be removed in init view mode");
    }

    if (object_type == SAI_OBJECT_TYPE_SWITCH)
    {
        /*
         * NOTE: Special care needs to be taken to clear all this
         * switch id's from all db's currently we skip this since we
         * assume that orchagent will not be removing just created
         * switches. But it may happen when asic will fail etc.
         *
         * To support multiple switches this case must be refactored.
         */

        SWSS_LOG_THROW("remove switch (%s) is not supported in init view mode yet! FIXME", str_object_id.c_str());
    }

    auto info = sai_metadata_get_object_type_info(object_type);

    if (info->isobjectid)
    {
        /*
         * If object is existing object (like bridge port, vlan member)
         * user may want to remove them, but this is temporary view,
         * and when we receive apply view, we will populate existing
         * objects to temporary view (since not all of them user may
         * query) and this will produce conflict, since some of those
         * objects user could explicitly remove. So to solve that we
         * need to have a list of removed objects, and then only
         * populate objects which not exist on removed list.
         */

        sai_object_id_t object_vid;
        sai_deserialize_object_id(str_object_id, object_vid);

        initViewRemovedVidSet.insert(object_vid);
    }

    sendApiResponse(SAI_COMMON_API_REMOVE, SAI_STATUS_SUCCESS);

    return SAI_STATUS_SUCCESS;
}

sai_status_t initViewSet(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    // we support SET api on all objects in init view mode.

    sendApiResponse(SAI_COMMON_API_SET, SAI_STATUS_SUCCESS);

    return SAI_STATUS_SUCCESS;
}

sai_status_t initViewGet(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    sai_status_t status;

    auto info = sai_metadata_get_object_type_info(object_type);

    if (info->isnonobjectid)
    {
        /*
         * Those objects are user created, so if user created ROUTE he
         * passed some attributes, there is no sense to support GET
         * since user explicitly know what attributes were set, similar
         * for other non object id types.
         */

        SWSS_LOG_ERROR("get is not supported on %s in init view mode", sai_serialize_object_type(object_type).c_str());

        status = SAI_STATUS_NOT_SUPPORTED;
    }
    else
    {
        sai_object_id_t object_id;
        sai_deserialize_object_id(str_object_id, object_id);

        SWSS_LOG_DEBUG("generic get (init view) for object type %s:%s",
                sai_serialize_object_type(object_type).c_str(),
                str_object_id.c_str());

        /*
         * Object must exists, we can't call GET on created object
         * in init view mode, get here can be called on existing
         * objects like default trap group to get some vendor
         * specific values.
         *
         * Exception here is switch, since all switches must be
         * created, when user will create switch on init view mode,
         * switch will be matched with existing switch, or it will
         * be explicitly created so user can query it properties.
         *
         * Translate vid to rid will make sure that object exist
         * and it have RID defined, so we can query it.
         */

        sai_object_id_t rid = g_translator->translateVidToRid(object_id);

        sai_object_meta_key_t meta_key;

        meta_key.objecttype = object_type;
        meta_key.objectkey.key.object_id = rid;

        status = g_vendorSai->get(meta_key, attr_count, attr_list);
    }

    sai_object_id_t switch_id;

    if (switches.size() == 1)
    {
        /*
         * We are in init view mode, but ether switch already
         * existed or first command was creating switch and user
         * created switch.
         *
         * We could change that later on, depends on object type we
         * can extract switch id, we could also have this method
         * inside metadata to get meta key.
         */

        switch_id = switches.begin()->second->getVid();
    }
    else
    {
        /*
         * This needs to be updated to support multiple switches
         * scenario.
         */

        SWSS_LOG_THROW("multiple switches are not supported yet: %zu", switches.size());
    }

    internal_syncd_get_send(object_type, str_object_id, switch_id, status, attr_count, attr_list);

    return status;
}

sai_status_t processQuadEventInInitViewMode(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ sai_common_api_t api,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    /*
     * Since attributes are not checked, it may happen that user will send some
     * invalid VID in object id/list in attribute, metadata should handle that,
     * but if that happen, this id will be treated as "new" object instead of
     * existing one.
     */

    switch (api)
    {
        case SAI_COMMON_API_CREATE:

            return initViewCreate(object_type, str_object_id, attr_count, attr_list);

        case SAI_COMMON_API_REMOVE:

            return initViewRemove(object_type, str_object_id);

        case SAI_COMMON_API_SET:

            return initViewSet(object_type, str_object_id, attr_list);

        case SAI_COMMON_API_GET:

            return initViewGet(object_type, str_object_id, attr_count, attr_list);

        default:

            SWSS_LOG_THROW("common api (%s) is not implemented in init view mode", sai_serialize_common_api(api).c_str());
    }
}

sai_status_t processQuadEvent(
        _In_ sai_common_api_t api,
        _In_ const swss::KeyOpFieldsValuesTuple &kco)
{
    SWSS_LOG_ENTER();

    const std::string &key = kfvKey(kco);
    const std::string &op = kfvOp(kco);

    const std::string &str_object_id = key.substr(key.find(":") + 1);

    sai_object_meta_key_t metaKey;
    sai_deserialize_object_meta_key(key, metaKey);

    if (!sai_metadata_is_object_type_valid(metaKey.objecttype))
    {
        SWSS_LOG_THROW("undefined object type %s", key.c_str());
    }

    const std::vector<swss::FieldValueTuple> &values = kfvFieldsValues(kco);

    for (const auto &v: values)
    {
        SWSS_LOG_DEBUG("attr: %s: %s", fvField(v).c_str(), fvValue(v).c_str());
    }

    SaiAttributeList list(metaKey.objecttype, values, false);

    /*
     * Attribute list can't be const since we will use it to translate VID to
     * RID in place.
     */

    sai_attribute_t *attr_list = list.get_attr_list();
    uint32_t attr_count = list.get_attr_count();

    /*
     * NOTE: This check pointers must be executed before init view mode, since
     * this methods replaces pointers from orchagent memory space to syncd
     * memory space.
     */

    if (metaKey.objecttype == SAI_OBJECT_TYPE_SWITCH && (api == SAI_COMMON_API_CREATE || api == SAI_COMMON_API_SET))
    {
        /*
         * We don't need to clear those pointers on switch remove (even last),
         * since those pointers will reside inside attributes, also sairedis
         * will internally check whether pointer is null or not, so we here
         * will receive all notifications, but redis only those that were set.
         */

        g_handler->updateNotificationsPointers(attr_count, attr_list);
    }

    if (g_syncd->isInitViewMode())
    {
        return processQuadEventInInitViewMode(metaKey.objecttype, str_object_id, api, attr_count, attr_list);
    }

    if (api != SAI_COMMON_API_GET)
    {
        /*
         * TODO we can also call translate on get, if sairedis will clean
         * buffer so then all OIDs will be NULL, and translation will also
         * convert them to NULL.
         */

        SWSS_LOG_DEBUG("translating VID to RIDs on all attributes");

        g_translator->translateVidToRid(metaKey.objecttype, attr_count, attr_list);
    }

    auto info = sai_metadata_get_object_type_info(metaKey.objecttype);

    sai_status_t status;

    if (info->isnonobjectid)
    {
        status = g_syncd->processEntry(metaKey, api, attr_count, attr_list);
    }
    else
    {
        status = processOid(metaKey.objecttype, str_object_id, api, attr_count, attr_list);
    }

    if (api == SAI_COMMON_API_GET)
    {
        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_DEBUG("get API for key: %s op: %s returned status: %s",
                    key.c_str(),
                    op.c_str(),
                    sai_serialize_status(status).c_str());
        }

        // extract switch VID from any object type

        sai_object_id_t switch_vid = VidManager::switchIdQuery(metaKey.objectkey.key.object_id);

        internal_syncd_get_send(metaKey.objecttype, str_object_id, switch_vid, status, attr_count, attr_list);
    }
    else if (status != SAI_STATUS_SUCCESS)
    {
        sendApiResponse(api, status);

        if (info->isobjectid && api == SAI_COMMON_API_SET)
        {
            sai_object_id_t vid;
            sai_deserialize_object_id(str_object_id, vid);

            sai_object_id_t rid = g_translator->translateVidToRid(vid);

            SWSS_LOG_ERROR("VID: %s RID: %s",
                    sai_serialize_object_id(vid).c_str(),
                    sai_serialize_object_id(rid).c_str());
        }

        for (const auto &v: values)
        {
            SWSS_LOG_ERROR("attr: %s: %s", fvField(v).c_str(), fvValue(v).c_str());
        }

        SWSS_LOG_THROW("failed to execute api: %s, key: %s, status: %s",
                op.c_str(),
                key.c_str(),
                sai_serialize_status(status).c_str());
    }
    else // non GET api, status is SUCCESS
    {
        sendApiResponse(api, status);
    }

    return status;
}

void processFlexCounterGroupEvent(
        _In_ swss::ConsumerTable &consumer)
{
    SWSS_LOG_ENTER();

    swss::KeyOpFieldsValuesTuple kco;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        consumer.pop(kco);
    }

    auto& groupName = kfvKey(kco);
    auto& op = kfvOp(kco);
    auto& values = kfvFieldsValues(kco);

    if (op == SET_COMMAND)
    {
        g_syncd->m_manager->addCounterPlugin(groupName, values);
    }
    else if (op == DEL_COMMAND)
    {
        g_syncd->m_manager->removeCounterPlugins(groupName);
    }
    else
    {
        SWSS_LOG_ERROR("unknown command: %s", op.c_str());
    }
}

void processFlexCounterEvent(
        _In_ swss::ConsumerTable &consumer)
{
    SWSS_LOG_ENTER();

    swss::KeyOpFieldsValuesTuple kco;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        consumer.pop(kco);
    }

    const auto &key = kfvKey(kco);
    std::string op = kfvOp(kco);

    std::size_t delimiter = key.find_first_of(":");
    if (delimiter == std::string::npos)
    {
        SWSS_LOG_ERROR("Failed to parse the key %s", key.c_str());

        return; // if key is invalid there is no need to process this event again
    }

    const auto groupName = key.substr(0, delimiter);
    const auto vidStr = key.substr(delimiter+1);

    sai_object_id_t vid;
    sai_deserialize_object_id(vidStr, vid);

    sai_object_id_t rid;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_translator->tryTranslateVidToRid(vid, rid))
        {
            SWSS_LOG_WARN("port VID %s, was not found (probably port was removed/splitted) and will remove from counters now",
              sai_serialize_object_id(vid).c_str());

            op = DEL_COMMAND;
        }
    }

    const auto values = kfvFieldsValues(kco);

    if (op == SET_COMMAND)
    {
        g_syncd->m_manager->addCounter(vid, rid, groupName, values);
    }
    else if (op == DEL_COMMAND)
    {
        g_syncd->m_manager->removeCounter(vid, groupName);
    }
}

void handleProfileMap(const std::string& profileMapFile)
{
    SWSS_LOG_ENTER();

    if (profileMapFile.size() == 0)
    {
        return;
    }

    std::ifstream profile(profileMapFile);

    if (!profile.is_open())
    {
        SWSS_LOG_ERROR("failed to open profile map file: %s : %s", profileMapFile.c_str(), strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Provide default value at boot up time and let sai profile value
    // Override following values if existing.
    // SAI reads these values at start up time. It would be too late to
    // set these values later when WARM BOOT is detected.
    gProfileMap[SAI_KEY_WARM_BOOT_WRITE_FILE] = DEF_SAI_WARM_BOOT_DATA_FILE;
    gProfileMap[SAI_KEY_WARM_BOOT_READ_FILE]  = DEF_SAI_WARM_BOOT_DATA_FILE;

    std::string line;

    while(getline(profile, line))
    {
        if (line.size() > 0 && (line[0] == '#' || line[0] == ';'))
        {
            continue;
        }

        size_t pos = line.find("=");

        if (pos == std::string::npos)
        {
            SWSS_LOG_WARN("not found '=' in line %s", line.c_str());
            continue;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        gProfileMap[key] = value;

        SWSS_LOG_INFO("insert: %s:%s", key.c_str(), value.c_str());
    }
}

typedef enum _syncd_restart_type_t
{
    SYNCD_RESTART_TYPE_COLD,

    SYNCD_RESTART_TYPE_WARM,

    SYNCD_RESTART_TYPE_FAST,

    SYNCD_RESTART_TYPE_PRE_SHUTDOWN,

} syncd_restart_type_t;

syncd_restart_type_t handleRestartQuery(swss::NotificationConsumer &restartQuery)
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

    if (switches.size() != 0)
    {
        SWSS_LOG_THROW("performing hard reinit, but there are %zu switches defined, bug!", switches.size());
    }

    try{
   // swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);
    HardReiniter hr(g_vendorSai);

    hr.hardReinit();
    }
    catch (std::exception&e)
    {
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);
    throw;
    }
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);
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

    sn.onFdbEvent = std::bind(&NotificationHandler::onFdbEvent, *g_handler, _1, _2);
    sn.onPortStateChange = std::bind(&NotificationHandler::onPortStateChange, *g_handler, _1, _2);
    sn.onQueuePfcDeadlock = std::bind(&NotificationHandler::onQueuePfcDeadlock, *g_handler, _1, _2);
    sn.onSwitchShutdownRequest = std::bind(&NotificationHandler::onSwitchShutdownRequest, *g_handler, _1);
    sn.onSwitchStateChange = std::bind(&NotificationHandler::onSwitchStateChange, *g_handler, _1, _2);

    g_handler->setSwitchNotifications(sn.getSwitchNotifications());

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
    sai_metadata_log = &sai_meta_log_syncd;
#pragma GCC diagnostic pop

    // TODO move to syncd object
    g_commandLineOptions = CommandLineOptionsParser::parseCommandLine(argc, argv);

    g_syncd = std::make_shared<Syncd>(g_commandLineOptions, isWarmStart);

    SWSS_LOG_NOTICE("command line: %s", g_commandLineOptions->getCommandLineString().c_str());

    handleProfileMap(g_commandLineOptions->m_profileMapFile);

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
    std::shared_ptr<swss::NotificationConsumer> restartQuery = std::make_shared<swss::NotificationConsumer>(dbAsic.get(), "RESTARTQUERY");

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

    getResponse  = std::make_shared<swss::ProducerTable>(dbAsic.get(), "GETRESPONSE");
    notifications = std::make_shared<swss::NotificationProducer>(dbNtf.get(), "NOTIFICATIONS");

    std::string fdbFlushLuaScript = swss::loadLuaScript(fdbFlushLuaScriptName);
    fdbFlushSha = swss::loadRedisScript(dbAsic.get(), fdbFlushLuaScript);

    g_veryFirstRun = isVeryFirstRun();

    /* ignore warm logic here if syncd starts in Mellanox fastfast boot mode */
    if (isWarmStart && (g_commandLineOptions->m_startType != SAI_START_TYPE_FASTFAST_BOOT))
    {
        g_commandLineOptions->m_startType = SAI_START_TYPE_WARM_BOOT;
    }

    if (g_commandLineOptions->m_startType == SAI_START_TYPE_WARM_BOOT)
    {
        const char *warmBootReadFile = profile_get_value(0, SAI_KEY_WARM_BOOT_READ_FILE);

        SWSS_LOG_NOTICE("using warmBootReadFile: '%s'", warmBootReadFile);

        if (warmBootReadFile == NULL || access(warmBootReadFile, F_OK) == -1)
        {
            SWSS_LOG_WARN("user requested warmStart but warmBootReadFile is not specified or not accesible, forcing cold start");

            g_commandLineOptions->m_startType = SAI_START_TYPE_COLD_BOOT;
        }
    }

    if (g_commandLineOptions->m_startType == SAI_START_TYPE_WARM_BOOT && g_veryFirstRun)
    {
        SWSS_LOG_WARN("warm start requested, but this is very first syncd start, forcing cold start");

        /*
         * We force cold start since if it's first run then redis db is not
         * complete so redis asic view will not reflect warm boot asic state,
         * if this happen then orch agent needs to be restarted as well to
         * repopulate asic view.
         */

        g_commandLineOptions->m_startType = SAI_START_TYPE_COLD_BOOT;
    }

    if (g_commandLineOptions->m_startType == SAI_START_TYPE_FASTFAST_BOOT)
    {
        /*
         * Mellanox SAI requires to pass SAI_WARM_BOOT as SAI_BOOT_KEY
         * to start 'fastfast'
         */
        gProfileMap[SAI_KEY_BOOT_TYPE] = std::to_string(SAI_START_TYPE_WARM_BOOT);
    } else {
        gProfileMap[SAI_KEY_BOOT_TYPE] = std::to_string(g_commandLineOptions->m_startType); // number value is needed
    }

    ServiceMethodTable smt;

    smt.profileGetValue = &profile_get_value;
    smt.profileGetNextValue = &profile_get_next_value;

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
                processFlexCounterEvent(*(swss::ConsumerTable*)sel);
            }
            else if (sel == flexCounterGroup.get())
            {
                processFlexCounterGroupEvent(*(swss::ConsumerTable*)sel);
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
        const char *warmBootWriteFile = profile_get_value(0, SAI_KEY_WARM_BOOT_WRITE_FILE);

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
