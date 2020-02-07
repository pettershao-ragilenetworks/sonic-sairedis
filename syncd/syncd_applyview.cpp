#include "syncd.h"
#include "sairediscommon.h"

#include "CommandLineOptions.h"
#include "VidManager.h"
#include "ComparisonLogic.h"
#include "Syncd.h"

using namespace syncd;

extern std::shared_ptr<CommandLineOptions> g_commandLineOptions; // TODO move to syncd object

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
