#include "HardReiniter.h"
#include "VidManager.h"
#include "SingleReiniter.h"

#include "sairediscommon.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"

#include "syncd.h" // TODO to be removed

using namespace syncd;

HardReiniter::HardReiniter(
        _In_ std::shared_ptr<sairedis::SaiInterface> sai):
    m_vendorSai(sai)
{
    SWSS_LOG_ENTER();

    // empty
}

HardReiniter::~HardReiniter()
{
    SWSS_LOG_ENTER();

    // empty
}

std::vector<std::string> HardReiniter::redisGetAsicStateKeys()
{
    SWSS_LOG_ENTER();

    return g_redisClient->keys(ASIC_STATE_TABLE + std::string(":*"));
}

std::unordered_map<sai_object_id_t, sai_object_id_t> HardReiniter::redisGetObjectMap(
        _In_ const std::string &key)
{
    SWSS_LOG_ENTER();

    auto hash = g_redisClient->hgetall(key);

    std::unordered_map<sai_object_id_t, sai_object_id_t> map;

    for (auto &kv: hash)
    {
        const std::string &str_key = kv.first;
        const std::string &str_value = kv.second;

        sai_object_id_t objectIdKey;
        sai_object_id_t objectIdValue;

        sai_deserialize_object_id(str_key, objectIdKey);

        sai_deserialize_object_id(str_value, objectIdValue);

        map[objectIdKey] = objectIdValue;
    }

    return map;
}

std::unordered_map<sai_object_id_t, sai_object_id_t> HardReiniter::redisGetVidToRidMap()
{
    SWSS_LOG_ENTER();

    // NOTE: To support multiple switches VIDTORID must be per switch.

    return redisGetObjectMap(VIDTORID);
}

std::unordered_map<sai_object_id_t, sai_object_id_t> HardReiniter::redisGetRidToVidMap()
{
    SWSS_LOG_ENTER();

    // NOTE: To support multiple switches RIDTOVID must be per switch.

    return redisGetObjectMap(RIDTOVID);
}

void HardReiniter::readAsicState()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_TIMER("read asic state");

    // Repopulate asic view from redis db after hard asic initialize.

    m_vidToRidMap = redisGetVidToRidMap();
    m_ridToVidMap = redisGetRidToVidMap();

    for (auto& v2r: m_vidToRidMap)
    {
        auto switchId = VidManager::switchIdQuery(v2r.first);

        m_switchVidToRid[switchId][v2r.first] = v2r.second;
    }

    for (auto& r2v: m_ridToVidMap)
    {
        auto switchId = VidManager::switchIdQuery(r2v.second);

        m_switchRidToVid[switchId][r2v.first] = r2v.second;
    }

    std::vector<std::string> asicStateKeys = redisGetAsicStateKeys();

    for (const auto &key: asicStateKeys)
    {
        auto mk = key.substr(key.find_first_of(":") + 1); // skip asic key

        sai_object_meta_key_t metaKey;
        sai_deserialize_object_meta_key(mk, metaKey);

        // if object is non object id then first item will be switch id

        auto switchId = VidManager::switchIdQuery(metaKey.objectkey.key.object_id);

        m_switchMap[switchId].push_back(key);
    }

    SWSS_LOG_NOTICE("loaded %zu switches", m_switchMap.size());

    for (auto& kvp: m_switchMap)
    {
        SWSS_LOG_NOTICE("switch VID: %s", sai_serialize_object_id(kvp.first).c_str());
    }
}

std::map<sai_object_id_t, std::shared_ptr<syncd::SaiSwitch>> HardReiniter::hardReinit()
{
    SWSS_LOG_ENTER();

    readAsicState();
    
    std::vector<std::shared_ptr<SingleReiniter>> vec;

    // perform hard reinit on all switches

    for (auto& kvp: m_switchMap)
    {
        auto sr = std::make_shared<SingleReiniter>(
                m_vendorSai,
                m_switchVidToRid.at(kvp.first),
                m_switchRidToVid.at(kvp.first),
                kvp.second);

        sr->hardReinit();

        vec.push_back(sr);
    }

    // since vid and rid maps contains all switches
    // we need to combine them

    ObjectIdMap vid2rid;
    ObjectIdMap rid2vid;

    for (auto&sr: vec)
    {
        auto map = sr->getTranslatedVid2Rid();

        for (auto&kvp: map)
        {
            vid2rid[kvp.first] = kvp.second;
            rid2vid[kvp.second] = kvp.first;
        }
    }

    if (vid2rid.size() != rid2vid.size())
    {
        SWSS_LOG_THROW("FATAL: vid2rid %zu != rid2vid %zu",
                vid2rid.size(),
                rid2vid.size());
    }

    // now some object could be removed from switch then we need to execute
    // post actions, and since those actions will be executed on switches which
    // will also modify redis database we need to execute this after we put vid
    // and rid map

    redisSetVidAndRidMap(vid2rid);

    std::map<sai_object_id_t, std::shared_ptr<syncd::SaiSwitch>> switches;

    for (auto& sr: vec)
    {
        sr->postRemoveActions();

        auto sw = sr->getSwitch();

        switches[sw->getVid()] = sw;
    }

    return switches;
}

void HardReiniter::redisSetVidAndRidMap(
        _In_ const std::unordered_map<sai_object_id_t, sai_object_id_t> &map)
{
    SWSS_LOG_ENTER();

    /*
     * TODO clear can be done after recreating all switches unless vid/rid map
     * will be per switch. An all this must be ATOMIC.
     *
     * This needs to be addressed when we want to support multiple switches.
     */

    redisClearVidToRidMap();
    redisClearRidToVidMap();

    for (auto &kv: map)
    {
        std::string strVid = sai_serialize_object_id(kv.first);
        std::string strRid = sai_serialize_object_id(kv.second);

        g_redisClient->hset(VIDTORID, strVid, strRid);
        g_redisClient->hset(RIDTOVID, strRid, strVid);
    }
}
