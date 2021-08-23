#include "SwitchBCM56850.h"

#include "meta/sai_serialize.h"

#include <gtest/gtest.h>

using namespace saivs;

TEST(SwitchBCM56850, ctr)
{
    auto sc = std::make_shared<SwitchConfig>(0, "");
    auto signal = std::make_shared<Signal>();
    auto eventQueue = std::make_shared<EventQueue>(signal);

    sc->m_saiSwitchType = SAI_SWITCH_TYPE_NPU;
    sc->m_switchType = SAI_VS_SWITCH_TYPE_BCM56850;
    sc->m_bootType = SAI_VS_BOOT_TYPE_COLD;
    sc->m_useTapDevice = false;
    sc->m_laneMap = LaneMap::getDefaultLaneMap(0);
    sc->m_eventQueue = eventQueue;

    auto scc = std::make_shared<SwitchConfigContainer>();

    scc->insert(sc);

    SwitchBCM56850 sw(
            0x2100000000,
            std::make_shared<RealObjectIdManager>(0, scc),
            sc);

    SwitchBCM56850 sw2(
            0x2100000000,
            std::make_shared<RealObjectIdManager>(0, scc),
            sc,
            nullptr);

    sai_attribute_t attr;
    
    attr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
    attr.value.booldata = true;

    EXPECT_EQ(sw.initialize_default_objects(1, &attr), SAI_STATUS_SUCCESS);
}

TEST(SwitchBCM56850, refresh_bridge_port_list)
{
    auto sc = std::make_shared<SwitchConfig>(0, "");
    auto signal = std::make_shared<Signal>();
    auto eventQueue = std::make_shared<EventQueue>(signal);

    sc->m_saiSwitchType = SAI_SWITCH_TYPE_NPU;
    sc->m_switchType = SAI_VS_SWITCH_TYPE_BCM56850;
    sc->m_bootType = SAI_VS_BOOT_TYPE_COLD;
    sc->m_useTapDevice = false;
    sc->m_laneMap = LaneMap::getDefaultLaneMap(0);
    sc->m_eventQueue = eventQueue;

    auto scc = std::make_shared<SwitchConfigContainer>();

    scc->insert(sc);

    SwitchBCM56850 sw(
            0x2100000000,
            std::make_shared<RealObjectIdManager>(0, scc),
            sc);

    sai_attribute_t attr;
    
    attr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
    attr.value.booldata = true;

    EXPECT_EQ(sw.initialize_default_objects(1, &attr), SAI_STATUS_SUCCESS);

    attr.id = SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID;
    attr.value.oid = SAI_NULL_OBJECT_ID;

    EXPECT_EQ(sw.get(SAI_OBJECT_TYPE_SWITCH, "oid:0x2100000000", 1, &attr), SAI_STATUS_SUCCESS);

    EXPECT_NE(attr.value.oid, SAI_NULL_OBJECT_ID);

    auto boid = attr.value.oid;

    auto sboid = sai_serialize_object_id(boid);

    sai_object_id_t list[128];

    attr.id = SAI_BRIDGE_ATTR_PORT_LIST;

    attr.value.objlist.count = 128;
    attr.value.objlist.list = list;

    EXPECT_EQ(sw.get(SAI_OBJECT_TYPE_BRIDGE, sboid, 1, &attr), SAI_STATUS_SUCCESS);
    
    //std::cout << sw.dump_switch_database_for_warm_restart();
}

static std::map<sai_object_id_t, WarmBootState> g_warmBootState;
// TODO move to utils
static bool getWarmBootState(
        _In_ const char* warmBootFile,
        _In_ std::shared_ptr<RealObjectIdManager> roidm)
{
    SWSS_LOG_ENTER();

    std::ifstream ifs;

    ifs.open(warmBootFile);

    if (!ifs.is_open())
    {
        SWSS_LOG_ERROR("failed to open: %s", warmBootFile);

        return false;
    }

    std::string line;

    while (std::getline(ifs, line))
    {
        SWSS_LOG_DEBUG("line: %s", line.c_str());

        // line format: OBJECT_TYPE OBJECT_ID ATTR_ID ATTR_VALUE
        std::istringstream iss(line);

        std::string strObjectType;
        std::string strObjectId;
        std::string strAttrId;
        std::string strAttrValue;

        iss >> strObjectType >> strObjectId;

        if (strObjectType == SAI_VS_FDB_INFO)
        {
            /*
             * If we read line from fdb info set and use tap device is enabled
             * just parse line and repopulate fdb info set.
             */

            FdbInfo fi = FdbInfo::deserialize(strObjectId);

            auto switchId = roidm->switchIdQuery(fi.m_portId);

            if (switchId == SAI_NULL_OBJECT_ID)
            {
                SWSS_LOG_ERROR("switchIdQuery returned NULL on fi.m_port = %s",
                        sai_serialize_object_id(fi.m_portId).c_str());

                g_warmBootState.clear();
                return false;
            }

            g_warmBootState[switchId].m_switchId = switchId;

            g_warmBootState[switchId].m_fdbInfoSet.insert(fi);

            continue;
        }

        iss >> strAttrId >> strAttrValue;

        sai_object_meta_key_t metaKey;
        sai_deserialize_object_meta_key(strObjectType + ":" + strObjectId, metaKey);

        /*
         * Since all objects we are creating, then during warm boot we need to
         * get the biggest object index, so after warm boot we can start
         * generating new objects with index value not colliding with objects
         * loaded from warm boot scenario. We only need to consider OID
         * objects.
         */

        roidm->updateWarmBootObjectIndex(metaKey.objectkey.key.object_id);

        // query each object for switch id

        auto switchId = roidm->switchIdQuery(metaKey.objectkey.key.object_id);

        if (switchId == SAI_NULL_OBJECT_ID)
        {
            SWSS_LOG_ERROR("switchIdQuery returned NULL on oid = %s",
                    sai_serialize_object_id(metaKey.objectkey.key.object_id).c_str());

            g_warmBootState.clear();
            return false;
        }

        g_warmBootState[switchId].m_switchId = switchId;

        auto &objectHash = g_warmBootState[switchId].m_objectHash[metaKey.objecttype]; // will create if not exist

        if (objectHash.find(strObjectId) == objectHash.end())
        {
            objectHash[strObjectId] = {};
        }

        if (strAttrId == "NULL")
        {
            // skip empty attributes
            continue;
        }

        objectHash[strObjectId][strAttrId] =
            std::make_shared<SaiAttrWrap>(strAttrId, strAttrValue);
    }

    // NOTE notification pointers should be restored by attr_list when creating switch

    ifs.close();

    return true;
}

TEST(SwitchBCM56850, warm_update_queues)
{
    auto sc = std::make_shared<SwitchConfig>(0, "");
    auto signal = std::make_shared<Signal>();
    auto eventQueue = std::make_shared<EventQueue>(signal);

    sc->m_saiSwitchType = SAI_SWITCH_TYPE_NPU;
    sc->m_switchType = SAI_VS_SWITCH_TYPE_BCM56850;
    sc->m_bootType = SAI_VS_BOOT_TYPE_COLD;
    sc->m_useTapDevice = false;
    sc->m_laneMap = LaneMap::getDefaultLaneMap(0);
    sc->m_eventQueue = eventQueue;

    auto scc = std::make_shared<SwitchConfigContainer>();

    scc->insert(sc);

    auto roidm = std::make_shared<RealObjectIdManager>(0, scc);

    EXPECT_TRUE(getWarmBootState("files/mlnx2700.warm.bin", roidm));

    auto warmBootState = std::make_shared<WarmBootState>(g_warmBootState.at(0x2100000000)); // copy ctr

    SwitchBCM56850 sw(
            0x2100000000,
            roidm,
            sc,
            warmBootState);

    sai_attribute_t attr;
    
    attr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
    attr.value.booldata = true;

    EXPECT_EQ(sw.warm_boot_initialize_objects(), SAI_STATUS_SUCCESS);

    attr.id = SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID;
    attr.value.oid = SAI_NULL_OBJECT_ID;

    EXPECT_EQ(sw.get(SAI_OBJECT_TYPE_SWITCH, "oid:0x2100000000", 1, &attr), SAI_STATUS_SUCCESS);

    EXPECT_NE(attr.value.oid, SAI_NULL_OBJECT_ID);

    auto boid = attr.value.oid;

    auto sboid = sai_serialize_object_id(boid);

    sai_object_id_t list[128];

    attr.id = SAI_BRIDGE_ATTR_PORT_LIST;

    attr.value.objlist.count = 128;
    attr.value.objlist.list = list;

    EXPECT_EQ(sw.get(SAI_OBJECT_TYPE_BRIDGE, sboid, 1, &attr), SAI_STATUS_SUCCESS);
}
