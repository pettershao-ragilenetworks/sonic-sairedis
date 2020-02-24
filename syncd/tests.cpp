#include <arpa/inet.h>

extern "C" {
#include <sai.h>
}

#include "swss/logger.h"
#include "swss/dbconnector.h"
#include "swss/schema.h"
#include "swss/redisreply.h"
#include "sairedis.h"
#include "meta/sai_serialize.h"
#include "syncd.h"
#include "sairediscommon.h"

#include "meta/OidRefCounter.h"
#include "meta/SaiAttrWrapper.h"
#include "meta/SaiObjectCollection.h"

#include <map>
#include <unordered_map>
#include <vector>
#include <thread>
#include <tuple>

// TODO remove when SAI will introduce bulk APIs to those objects
sai_status_t sai_bulk_create_fdb_entry(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t *fdb_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses);

sai_status_t sai_bulk_remove_fdb_entry(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t *fdb_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses);

extern bool g_syncMode;

#define ASSERT_SUCCESS(format,...) \
    if ((status)!=SAI_STATUS_SUCCESS) \
        SWSS_LOG_THROW(format ": %s", ##__VA_ARGS__, sai_serialize_status(status).c_str());

using namespace saimeta;
extern std::shared_ptr<swss::RedisClient>   g_redisClient;
static std::shared_ptr<swss::DBConnector> g_db1;

static sai_next_hop_group_api_t test_next_hop_group_api;
static std::vector<std::tuple<sai_object_id_t, sai_object_id_t, std::vector<sai_attribute_t>>> created_next_hop_group_member;

sai_status_t test_create_next_hop_group_member(
        _Out_ sai_object_id_t *next_hop_group_member_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    created_next_hop_group_member.emplace_back();
    auto& back = created_next_hop_group_member.back();
    std::get<0>(back) = *next_hop_group_member_id;
    std::get<1>(back) = switch_id;
    auto& attrs = std::get<2>(back);
    attrs.insert(attrs.end(), attr_list, attr_list + attr_count);
    return 0;
}

void clearDB()
{
    SWSS_LOG_ENTER();

    g_db1 = std::make_shared<swss::DBConnector>(ASIC_DB, "localhost", 6379, 0);
    swss::RedisReply r(g_db1.get(), "FLUSHALL", REDIS_REPLY_STATUS);
    g_redisClient = std::make_shared<swss::RedisClient>(g_db1.get());

    r.checkStatusOK();
}

static const char* profile_get_value(
        _In_ sai_switch_profile_id_t profile_id,
        _In_ const char* variable)
{
    SWSS_LOG_ENTER();

    return NULL;
}

static int profile_get_next_value(
        _In_ sai_switch_profile_id_t profile_id,
        _Out_ const char** variable,
        _Out_ const char** value)
{
    SWSS_LOG_ENTER();

    if (value == NULL)
    {
        SWSS_LOG_INFO("resetting profile map iterator");

        return 0;
    }

    if (variable == NULL)
    {
        SWSS_LOG_WARN("variable is null");
        return -1;
    }

    SWSS_LOG_INFO("iterator reached end");
    return -1;
}

static sai_service_method_table_t test_services = {
    profile_get_value,
    profile_get_next_value
};

void sai_reinit()
{
    SWSS_LOG_ENTER();

    clearDB();

    sai_api_uninitialize();
    sai_api_initialize(0, (sai_service_method_table_t*)&test_services);
}

void test_sai_initialize()
{
    SWSS_LOG_ENTER();

    // NOTE: this is just testing whether test application will
    // link against libsairedis, this api requires running redis db
    // with enabled unix socket
    sai_status_t status = sai_api_initialize(0, (sai_service_method_table_t*)&test_services);

    // Mock the SAI api
    test_next_hop_group_api.create_next_hop_group_member = test_create_next_hop_group_member;
    sai_metadata_sai_next_hop_group_api = &test_next_hop_group_api;
    created_next_hop_group_member.clear();

    ASSERT_SUCCESS("Failed to initialize api");
}

void test_enable_recording()
{
    SWSS_LOG_ENTER();

    sai_attribute_t attr;
    attr.id = SAI_REDIS_SWITCH_ATTR_RECORD;
    attr.value.booldata = true;

    sai_switch_api_t *sai_switch_api = NULL;

    sai_api_query(SAI_API_SWITCH, (void**)&sai_switch_api);

    sai_status_t status = sai_switch_api->set_switch_attribute(SAI_NULL_OBJECT_ID, &attr);

    ASSERT_SUCCESS("Failed to enable recording");
}

bool starts_with(const std::string& str, const std::string& substr)
{
    SWSS_LOG_ENTER();

    return strncmp(str.c_str(), substr.c_str(), substr.size()) == 0;
}

void bulk_nhgm_consumer_worker()
{
    SWSS_LOG_ENTER();

    std::string tableName = ASIC_STATE_TABLE;
    swss::DBConnector db("ASIC_DB", 0, true);
    swss::ConsumerTable c(&db, tableName);
    swss::Select cs;
    swss::Selectable *selectcs;
    int ret = 0;

    cs.addSelectable(&c);
    while ((ret = cs.select(&selectcs)) == swss::Select::OBJECT)
    {
        swss::KeyOpFieldsValuesTuple kco;
        c.pop(kco);

        auto& key = kfvKey(kco);
        auto& op = kfvOp(kco);

        if (starts_with(key, "SAI_OBJECT_TYPE_SWITCH")) continue;

        if (op == "bulkcreate")
        {
            sai_status_t status = SAI_STATUS_FAILURE;

            try
            {
                status = processBulkEvent((sai_common_api_t)SAI_COMMON_API_BULK_CREATE, kco);
            }
            catch(std::exception&e)
            {
                SWSS_LOG_ERROR("got exception: %s", e.what());
            }
            ASSERT_SUCCESS("Failed to processBulkEvent");
            break;
        }

    }
}

void test_bulk_next_hop_group_member_create()
{
    SWSS_LOG_ENTER();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);

    sai_reinit();

  // auto consumerThreads = new std::thread(bulk_nhgm_consumer_worker);

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    sai_status_t    status;

    sai_next_hop_api_t  *sai_next_hop_api = NULL;
    sai_next_hop_group_api_t  *sai_next_hop_group_api = NULL;
    sai_switch_api_t *sai_switch_api = NULL;

    sai_api_query(SAI_API_NEXT_HOP, (void**)&sai_next_hop_api);
    sai_api_query(SAI_API_NEXT_HOP_GROUP, (void**)&sai_next_hop_group_api);
    sai_api_query(SAI_API_SWITCH, (void**)&sai_switch_api);

    uint32_t count = 3;

    std::vector<sai_route_entry_t> routes;
    std::vector<sai_attribute_t> attrs;

    sai_attribute_t swattr;

    swattr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
    swattr.value.booldata = true;

    sai_object_id_t switch_id;
    status = sai_switch_api->create_switch(&switch_id, 1, &swattr);

    ASSERT_SUCCESS("Failed to create switch");

    std::vector<std::vector<sai_attribute_t>> nhgm_attrs;
    std::vector<const sai_attribute_t *> nhgm_attrs_array;
    std::vector<uint32_t> nhgm_attrs_count;

    sai_object_id_t hopgroup_vid;
    sai_attribute_t nhgattr;

    nhgattr.id = SAI_NEXT_HOP_GROUP_ATTR_TYPE;
    nhgattr.value.s32 = SAI_NEXT_HOP_GROUP_TYPE_ECMP;
    status = sai_next_hop_group_api->create_next_hop_group(&hopgroup_vid, switch_id, 1,&nhgattr);

    ASSERT_SUCCESS("failed to create next hop group");

    for (uint32_t i = 0; i <  count; ++i)
    {
        sai_object_id_t hop_vid;

        sai_attribute_t nhattr[3] = { };

        nhattr[0].id = SAI_NEXT_HOP_ATTR_TYPE;
        nhattr[0].value.s32 = SAI_NEXT_HOP_TYPE_SEGMENTROUTE_ENDPOINT;

        nhattr[1].id = SAI_NEXT_HOP_ATTR_SEGMENTROUTE_ENDPOINT_TYPE;

        nhattr[2].id = SAI_NEXT_HOP_ATTR_SEGMENTROUTE_ENDPOINT_POP_TYPE;

        status = sai_next_hop_api->create_next_hop(&hop_vid, switch_id, 3, nhattr);

        ASSERT_SUCCESS("failed to create next hop");

        std::vector<sai_attribute_t> list(2);
        sai_attribute_t &attr1 = list[0];
        sai_attribute_t &attr2 = list[1];

        attr1.id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID;
        attr1.value.oid = hopgroup_vid;
        attr2.id = SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID;
        attr2.value.oid = hop_vid;

        nhgm_attrs.push_back(list);
        nhgm_attrs_count.push_back(2);
    }

    for (size_t j = 0; j < nhgm_attrs.size(); j++)
    {
        nhgm_attrs_array.push_back(nhgm_attrs[j].data());
    }

    std::vector<sai_status_t> statuses(count);
    std::vector<sai_object_id_t> object_id(count);
    sai_next_hop_group_api->create_next_hop_group_members(switch_id, count, nhgm_attrs_count.data(), nhgm_attrs_array.data(), SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR, object_id.data(), statuses.data());
    ASSERT_SUCCESS("Failed to bulk create nhgm");
    for (size_t j = 0; j < statuses.size(); j++)
    {
        status = statuses[j];
        ASSERT_SUCCESS("Failed to create nhgm # %zu", j);
    }

    return ;
    //consumerThreads->join();
    //delete consumerThreads;

    // check the created nhgm
    for (size_t i = 0; i < created_next_hop_group_member.size(); i++)
    {
        auto& created = created_next_hop_group_member[i];
        auto& created_attrs = std::get<2>(created);
        assert(created_attrs.size() == 2);
        assert(created_attrs[1].value.oid == nhgm_attrs[i][1].value.oid);
    }

    status = sai_next_hop_group_api->remove_next_hop_group_members(count, object_id.data(), SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR, statuses.data());
    ASSERT_SUCCESS("Failed to bulk remove nhgm");
}

void test_bulk_fdb_create()
{
    SWSS_LOG_ENTER();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);

    sai_reinit();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    sai_status_t    status;

    sai_switch_api_t *sai_switch_api = NULL;
    sai_lag_api_t *sai_lag_api = NULL;
    sai_bridge_api_t *sai_bridge_api = NULL;
    sai_virtual_router_api_t * sai_virtual_router_api = NULL;

    sai_api_query(SAI_API_BRIDGE, (void**)&sai_bridge_api);
    sai_api_query(SAI_API_LAG, (void**)&sai_lag_api);
    sai_api_query(SAI_API_SWITCH, (void**)&sai_switch_api);
    sai_api_query(SAI_API_VIRTUAL_ROUTER, (void**)&sai_virtual_router_api);

    uint32_t count = 3;

    std::vector<sai_fdb_entry_t> fdbs;

    uint32_t index = 15;

    sai_attribute_t swattr;

    swattr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
    swattr.value.booldata = true;

    sai_object_id_t switch_id;
    status = sai_switch_api->create_switch(&switch_id, 1, &swattr);

    ASSERT_SUCCESS("Failed to create switch");

    std::vector<std::vector<sai_attribute_t>> fdb_attrs;
    std::vector<const sai_attribute_t *> fdb_attrs_array;
    std::vector<uint32_t> fdb_attrs_count;

    for (uint32_t i = index; i < index + count; ++i)
    {
        // virtual router
        sai_object_id_t vr;

        status = sai_virtual_router_api->create_virtual_router(&vr, switch_id, 0, NULL);

        ASSERT_SUCCESS("failed to create virtual router");

        // bridge
        sai_object_id_t bridge;

        sai_attribute_t battr;

        battr.id = SAI_BRIDGE_ATTR_TYPE;
        battr.value.s32 = SAI_BRIDGE_TYPE_1Q;

        status = sai_bridge_api->create_bridge(&bridge, switch_id, 1, &battr);

        ASSERT_SUCCESS("failed to create bridge");

        sai_object_id_t lag;
        status = sai_lag_api->create_lag(&lag, switch_id, 0, NULL);

        ASSERT_SUCCESS("failed to create lag");

        // bridge port
        sai_object_id_t bridge_port;

        sai_attribute_t bpattr[2];

        bpattr[0].id = SAI_BRIDGE_PORT_ATTR_TYPE;
        bpattr[0].value.s32 = SAI_BRIDGE_PORT_TYPE_PORT;
        bpattr[1].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
        bpattr[1].value.oid = lag;

        status = sai_bridge_api->create_bridge_port(&bridge_port, switch_id, 2, bpattr);

        ASSERT_SUCCESS("failed to create bridge port");

        sai_fdb_entry_t fdb_entry;
        fdb_entry.switch_id = switch_id;
        memset(fdb_entry.mac_address, 0, sizeof(sai_mac_t));
        fdb_entry.mac_address[0] = 0xD;
        fdb_entry.bv_id = bridge;

        fdbs.push_back(fdb_entry);

        std::vector<sai_attribute_t> attrs;
        sai_attribute_t attr;

        attr.id = SAI_FDB_ENTRY_ATTR_TYPE;
        attr.value.s32 = (i % 2) ? SAI_FDB_ENTRY_TYPE_DYNAMIC : SAI_FDB_ENTRY_TYPE_STATIC;
        attrs.push_back(attr);

        attr.id = SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID;
        attr.value.oid = bridge_port;
        attrs.push_back(attr);

        attr.id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;
        attr.value.s32 = SAI_PACKET_ACTION_FORWARD;
        attrs.push_back(attr);

        fdb_attrs.push_back(attrs);
        fdb_attrs_count.push_back((unsigned int)attrs.size());
    }

    for (size_t j = 0; j < fdb_attrs.size(); j++)
    {
        fdb_attrs_array.push_back(fdb_attrs[j].data());
    }

    std::vector<sai_status_t> statuses(count);
    status = sai_bulk_create_fdb_entry(count, fdbs.data(), fdb_attrs_count.data(), (const sai_attribute_t**)fdb_attrs_array.data()
        , SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR, statuses.data());
    ASSERT_SUCCESS("Failed to create fdb");
    for (size_t j = 0; j < statuses.size(); j++)
    {
        status = statuses[j];
        ASSERT_SUCCESS("Failed to create fdb # %zu", j);
    }

    // Remove fdb entry
    status = sai_bulk_remove_fdb_entry(count, fdbs.data(), SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR, statuses.data());
    ASSERT_SUCCESS("Failed to bulk remove fdb entry");
}

void test_bulk_route_set()
{
    SWSS_LOG_ENTER();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);

    sai_reinit();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    sai_status_t    status;

    sai_route_api_t  *sai_route_api = NULL;
    sai_switch_api_t *sai_switch_api = NULL;
    sai_virtual_router_api_t * sai_virtual_router_api = NULL;
    sai_next_hop_api_t  *sai_next_hop_api = NULL;

    sai_api_query(SAI_API_ROUTE, (void**)&sai_route_api);
    sai_api_query(SAI_API_SWITCH, (void**)&sai_switch_api);
    sai_api_query(SAI_API_VIRTUAL_ROUTER, (void**)&sai_virtual_router_api);
    sai_api_query(SAI_API_NEXT_HOP, (void**)&sai_next_hop_api);

    uint32_t count = 3;

    std::vector<sai_route_entry_t> routes;
    std::vector<sai_attribute_t> attrs;

    uint32_t index = 15;

    sai_attribute_t swattr;

    swattr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
    swattr.value.booldata = true;

    sai_object_id_t switch_id;
    status = sai_switch_api->create_switch(&switch_id, 1, &swattr);

    ASSERT_SUCCESS("Failed to create switch");

    std::vector<std::vector<sai_attribute_t>> route_attrs;
    std::vector<const sai_attribute_t *> route_attrs_array;
    std::vector<uint32_t> route_attrs_count;

    for (uint32_t i = index; i < index + count; ++i)
    {
        sai_route_entry_t route_entry;

        // virtual router
        sai_object_id_t vr;

        status = sai_virtual_router_api->create_virtual_router(&vr, switch_id, 0, NULL);

        ASSERT_SUCCESS("failed to create virtual router");

        // next hop
        sai_object_id_t hop;

        sai_attribute_t nhattr[3] = { };

        nhattr[0].id = SAI_NEXT_HOP_ATTR_TYPE;
        nhattr[0].value.s32 = SAI_NEXT_HOP_TYPE_SEGMENTROUTE_ENDPOINT;
        nhattr[1].id = SAI_NEXT_HOP_ATTR_SEGMENTROUTE_ENDPOINT_TYPE;
        nhattr[2].id = SAI_NEXT_HOP_ATTR_SEGMENTROUTE_ENDPOINT_POP_TYPE;

        status = sai_next_hop_api->create_next_hop(&hop, switch_id, 3, nhattr);

        ASSERT_SUCCESS("failed to create next hop");

        route_entry.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        route_entry.destination.addr.ip4 = htonl(0x0a000000 | i);
        route_entry.destination.mask.ip4 = htonl(0xffffffff);
        route_entry.vr_id = vr;
        route_entry.switch_id = switch_id;
        route_entry.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        routes.push_back(route_entry);

        std::vector<sai_attribute_t> list(2);
        sai_attribute_t &attr1 = list[0];
        sai_attribute_t &attr2 = list[1];

        attr1.id = SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID;
        attr1.value.oid = hop;
        attr2.id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
        attr2.value.s32 = SAI_PACKET_ACTION_FORWARD;
        route_attrs.push_back(list);
        route_attrs_count.push_back(2);
    }

    for (size_t j = 0; j < route_attrs.size(); j++)
    {
        route_attrs_array.push_back(route_attrs[j].data());
    }

    std::vector<sai_status_t> statuses(count);
    status = sai_route_api->create_route_entries(count, routes.data(), route_attrs_count.data(), route_attrs_array.data(), SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR, statuses.data());
    ASSERT_SUCCESS("Failed to create route");
    for (size_t j = 0; j < statuses.size(); j++)
    {
        status = statuses[j];
        ASSERT_SUCCESS("Failed to create route # %zu", j);
    }

    for (uint32_t i = index; i < index + count; ++i)
    {
        sai_attribute_t attr;
        attr.id = SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION;
        attr.value.s32 = SAI_PACKET_ACTION_DROP;

        status = sai_route_api->set_route_entry_attribute(&routes[i - index], &attr);

        attrs.push_back(attr);

        ASSERT_SUCCESS("Failed to set route");
    }

    statuses.clear();
    statuses.resize(attrs.size());

    for (auto &attr: attrs)
    {
        attr.value.s32 = SAI_PACKET_ACTION_FORWARD;
    }

    status = sai_route_api->set_route_entries_attribute(
        count,
        routes.data(),
        attrs.data(),
        SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR,
        statuses.data());

    ASSERT_SUCCESS("Failed to bulk set route");

    for (auto s: statuses)
    {
        status = s;

        ASSERT_SUCCESS("Failed to bulk set route on one of the routes");
    }

    // TODO we need to add consumer producer test here to see
    // if after consume we get pop we get expected parameters

    // TODO in async mode this api will always return success
    // Remove route entry
    status = sai_route_api->remove_route_entries(count, routes.data(), SAI_BULK_OP_ERROR_MODE_IGNORE_ERROR, statuses.data());
    ASSERT_SUCCESS("Failed to bulk remove route entry");
}

int main()
{
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    SWSS_LOG_ENTER();

//    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_INFO);

    try
    {
        test_sai_initialize();

        // g_syncMode = true;

        test_enable_recording();

        test_bulk_next_hop_group_member_create();

        test_bulk_fdb_create();

        test_bulk_route_set();

        sai_api_uninitialize();

        printf("\n[ %s ]\n\n", sai_serialize_status(SAI_STATUS_SUCCESS).c_str());
    }
    catch (const std::exception &e)
    {
        SWSS_LOG_ERROR("exception: %s", e.what());

        printf("\n[ %s ]\n\n%s\n\n", sai_serialize_status(SAI_STATUS_FAILURE).c_str(), e.what());

        exit(EXIT_FAILURE);
    }

    return 0;
}
