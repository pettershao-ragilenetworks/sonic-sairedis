#include "syncd.h"
#include "sairediscommon.h"
#include "swss/table.h"
#include "swss/logger.h"
#include "swss/dbconnector.h"

#include "CommandLineOptions.h"
#include "SaiAttr.h"
#include "SaiObj.h"
#include "AsicView.h"
#include "VidManager.h"
#include "BestCandidateFinder.h"
#include "NotificationHandler.h"
#include "VirtualOidTranslator.h"
#include "ComparisonLogic.h"

#include <inttypes.h>
#include <algorithm>
#include <list>

using namespace syncd;

extern std::shared_ptr<CommandLineOptions> g_commandLineOptions; // TODO move to syncd object

/**
 * When set to true extra logging will be added for tracking references.  This
 * is useful for debugging, but for production operations this will produce too
 * much noise in logs, and we still can replay scenario using recordings.
 */
bool enableRefernceCountLogs = false;

void redisGetAsicView(
        _In_ const std::string &tableName,
        _In_ AsicView &view)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_TIMER("get asic view from %s", tableName.c_str());

    swss::DBConnector db("ASIC_DB", 0);

    swss::Table table(&db, tableName);

    swss::TableDump dump;

    table.dump(dump);

    view.fromDump(dump);

    SWSS_LOG_NOTICE("objects count for %s: %zu", tableName.c_str(), view.m_soAll.size());
}

// TODO we can do this in generic way, we need serialize

void updateRedisDatabase(
        _In_ const AsicView &currentView,
        _In_ const AsicView &temporaryView)
{
    SWSS_LOG_ENTER();

    /*
     * TODO: We can make LUA script for this which will be much faster.
     *
     * TODO: This needs to be updated if we want to support multiple switches.
     */

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

    // Save temporary view as current view in redis database.

    for (const auto &pair: temporaryView.m_soAll)
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

    /*
     * Remove previous RID2VID maps and apply new map.
     *
     * TODO: This needs to be done per switch, we can't remove all maps.
     */

    redisClearVidToRidMap();
    redisClearRidToVidMap();

    for (auto &kv: temporaryView.m_ridToVid)
    {
        std::string strVid = sai_serialize_object_id(kv.second);
        std::string strRid = sai_serialize_object_id(kv.first);

        g_redisClient->hset(VIDTORID, strVid, strRid);
        g_redisClient->hset(RIDTOVID, strRid, strVid);
    }

    SWSS_LOG_NOTICE("updated redis database");
}

extern std::set<sai_object_id_t> initViewRemovedVidSet;

// TODO all switches must include
void dumpComparisonLogicOutput(
        _In_ const AsicView &currentView)
{
    SWSS_LOG_ENTER();

    std::stringstream ss;

    ss << "ASIC_OPERATIONS: " << currentView.asicGetOperationsCount() << std::endl;

    for (const auto &op: currentView.asicGetWithOptimizedRemoveOperations())
    {
        const std::string &key = kfvKey(*op.m_op);
        const std::string &opp = kfvOp(*op.m_op);

        ss << "o " << opp << ": " << key << std::endl;

        const auto &values = kfvFieldsValues(*op.m_op);

        for (auto v: values)
            ss << "a: " << fvField(v) << " " << fvValue(v) << std::endl;
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

    std::srand((unsigned int)std::time(0));

    /*
     * NOTE: Current view can contain multiple switches at once but in our
     * implementation we only will have 1 switch.
     */

    AsicView current;
    AsicView temp;

    /*
     * We are starting first stage here, it still can throw exceptions but it's
     * non destructive for ASIC, so just catch and return failure.
     */

    std::shared_ptr<ComparisonLogic> cl;

    try
    {
        /*
         * NOTE: Need to be per switch. Asic view may contain multiple switches.
         *
         * In our current solution we will deal with only one switch, since
         * supporting multiple switches will have serious impact on redesign
         * current implementation.
         */

        if (switches.size() != 1)
        {
            /*
             * NOTE: In our solution multiple switches are not supported.
             */

            SWSS_LOG_THROW("only one switch is expected, got: %zu switches", switches.size());
        }

        // XXX we have only 1 switch, so we can get away with this

        auto sw = switches.begin()->second;

        AsicView::ObjectIdMap vidToRidMap = sw->redisGetVidToRidMap();
        AsicView::ObjectIdMap ridToVidMap = sw->redisGetRidToVidMap();

        current.m_ridToVid = ridToVidMap;
        current.m_vidToRid = vidToRidMap;

        /*
         * Those calls could be calls to SAI, but when this will be separate lib
         * then we would like to limit sai to minimum or reimplement getting those.
         *
         * TODO: This needs to be refactored and solved in other way since asic
         * view can contain multiple switches.
         *
         * TODO: This also can be optimized using metadata.
         * We need to add access to SaiSwitch in AsicView.
         */

        // TODO needs to be removed and done in generic
        current.m_defaultTrapGroupRid     = sw->getSwitchDefaultAttrOid(SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP);
        temp.m_defaultTrapGroupRid        = current.m_defaultTrapGroupRid;

        /*
         * Read current and temporary view from REDIS.
         */

        redisGetAsicView(ASIC_STATE_TABLE, current);
        redisGetAsicView(TEMP_PREFIX ASIC_STATE_TABLE, temp);

        /*
         * Match oids before calling populate existing objects since after
         * matching oids RID and VID maps will be populated.
         */

        cl = std::make_shared<ComparisonLogic>(g_vendorSai, sw, initViewRemovedVidSet);

        cl->matchOids(current, temp);

        /*
         * Populate existing objects to current and temp view if they don't
         * exist since we are populating them when syncd starts, and when we
         * switch view we don't want to loose any of those objects since during
         * syncd runtime is counting on that those objects exists.
         *
         * TODO: If some object's will be removed like VLAN members then this
         * existing objects needs to be updated in the switch!
         */

        const auto &existingObjects = sw->getDiscoveredRids();

        cl->populateExistingObjects(current, temp, existingObjects);

        cl->checkInternalObjects(current, temp);

        /*
         * Call main method!
         */

        if (enableRefernceCountLogs)
        {
            current.dumpRef("current START");
            temp.dumpRef("temp START");
        }

        cl->createPreMatchMap(current, temp);

        cl->logViewObjectCount(current, temp);

        cl->applyViewTransition(current, temp);

        SWSS_LOG_NOTICE("ASIC operations to execute: %zu", current.asicGetOperationsCount());

        temp.checkObjectsStatus();

        SWSS_LOG_NOTICE("all temporary view objects were processed to FINAL state");

        current.checkObjectsStatus();

        SWSS_LOG_NOTICE("all current view objects were processed to FINAL state");

        /*
         * After all operations both views should look the same so number of
         * rid/vid should look the same.
         */

        cl->checkMap(current, temp);

        /*
         * At the end number of m_soAll objects must be equal on both views. If
         * some on temporary views are missing, we need to transport empty
         * objects to temporary view, like queues, scheduler groups, virtual
         * router, trap groups etc.
         */

        if (current.m_soAll.size() != temp.m_soAll.size())
        {
            /*
             * If this will happen that means non object id values are
             * different since number of RID/VID maps is identical (previous
             * check).
             *
             * Unlikely to be routes/neighbors/fdbs, can be traps, switch,
             * vlan.
             *
             * TODO: For debug we will need to display differences
             */

            SWSS_LOG_THROW("wrong number of all objects current: %zu vs temp %zu, FIXME",
                    current.m_soAll.size(),
                    temp.m_soAll.size());
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

    if (g_commandLineOptions->m_enableUnittests) // TODO must include all ASICS
    {
        dumpComparisonLogicOutput(current);
    }

    cl->executeOperationsOnAsic(current, temp);

    updateRedisDatabase(current, temp);

    if (g_commandLineOptions->m_enableConsistencyCheck)
    {
        cl->checkAsicVsDatabaseConsistency(current, temp);
    }

    return SAI_STATUS_SUCCESS;
}


