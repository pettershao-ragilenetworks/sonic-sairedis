#include "syncd.h"
#include "sairediscommon.h"

#include "VidManager.h"
#include "NotificationHandler.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>

using namespace syncd;

extern std::shared_ptr<NotificationHandler> g_handler;
extern sai_object_id_t gSwitchId;

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

        SWSS_LOG_ERROR("RID 0x%lx is missing from current RID2VID map after WARM boot!", rid);

        success = false;
    }

    if (!success)
        SWSS_LOG_THROW("FATAL, some discovered RIDs are not present in current RID2VID map, bug");

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
        SAI_SWITCH_ATTR_PACKET_EVENT_NOTIFY,
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
        SWSS_LOG_THROW("gSwitchId already contain switch!, SAI THRIFT don't support multiple switches yer, FIXME");
    }

    gSwitchId = switch_rid; // TODO this is needed on warm boot
//#endif

    checkWarmBootDiscoveredRids(sw);

    startDiagShell();
}
