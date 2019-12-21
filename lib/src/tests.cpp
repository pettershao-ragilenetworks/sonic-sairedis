extern "C" {
#include "saimetadata.h"
}

#include "swss/logger.h"
#include "swss/table.h"

#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

#include <unistd.h>

#include <iostream>
#include <chrono>
#include <vector>

sai_object_type_t sai_object_type_query(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return SAI_OBJECT_TYPE_NULL;
}

sai_object_id_t sai_switch_id_query(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return SAI_NULL_OBJECT_ID;
}

sai_route_entry_t get_route_entry()
{
    SWSS_LOG_ENTER();

    sai_route_entry_t route_entry = { };

    route_entry.vr_id = 0x123456789abcdef;
    route_entry.switch_id = 0x123456789abcdef;
    route_entry.destination.addr.ip4 = 0x12345678;
    route_entry.destination.mask.ip4 = 0xffffffff;

    return route_entry;
}

std::string serialize_route_entry()
{
    SWSS_LOG_ENTER();

    static auto route_entry = get_route_entry();

    return sai_serialize_object_type(SAI_OBJECT_TYPE_ROUTE_ENTRY) + ":" +
        sai_serialize_route_entry(route_entry);
}

std::string serialize_route_entry2()
{
    SWSS_LOG_ENTER();

    static auto route_entry = get_route_entry();

    char buffer[1000];

    int n = sai_serialize_object_type(buffer, SAI_OBJECT_TYPE_ROUTE_ENTRY);
    buffer[n] = ':';

    sai_serialize_route_entry(buffer+n+1, &route_entry);

    return  std::string(buffer);
}

std::string serialize_vlan()
{
    SWSS_LOG_ENTER();

    sai_object_id_t oid = 0x123456789abcdef;

    return sai_serialize_object_type(SAI_OBJECT_TYPE_VLAN) + ":" +
        sai_serialize_object_id(oid);
}

void test_serialize_remove_route_entry(int n)
{
    SWSS_LOG_ENTER();

    std::cout << serialize_route_entry() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_route_entry();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    std::cout << "ms: " << (double)us.count()/1000 << " / " << n << std::endl;
}

void test_serialize_remove_oid(int n)
{
    SWSS_LOG_ENTER();

    std::cout << serialize_vlan() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_vlan();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    std::cout << "ms: " << (double)us.count()/1000 << " / " << n << std::endl;
}

sai_fdb_entry_t get_fdb_entry()
{
    SWSS_LOG_ENTER();

    std::string str = "{\"bvid\":\"oid:0x123456789abcdef\",\"mac\":\"52:54:00:79:16:93\",\"switch_id\":\"oid:0x123456789abcdef\"}";

    sai_fdb_entry_t fdb_entry;
    sai_deserialize_fdb_entry(str, fdb_entry);

    return fdb_entry;
}

std::string serialize_fdb_entry()
{
    SWSS_LOG_ENTER();

    static auto fdb_entry = get_fdb_entry();

    return sai_serialize_object_type(SAI_OBJECT_TYPE_FDB_ENTRY) + ":" +
        sai_serialize_fdb_entry(fdb_entry);
}

void test_serialize_remove_fdb_entry(int n)
{
    SWSS_LOG_ENTER();

    std::cout << serialize_fdb_entry() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_fdb_entry();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    std::cout << "ms: " << (double)us.count()/1000 << " / " << n << std::endl;
}

std::string serialize_bulk_vlan(int per)
{
    SWSS_LOG_ENTER();

    sai_object_id_t oid = 0x123456789abcdef;

    int n = per;

    std::string str_object_type = sai_serialize_object_type(SAI_OBJECT_TYPE_VLAN);

    std::vector<swss::FieldValueTuple> entries;

    for (int idx = 0; idx < n; ++idx)
    {
        std::string str_attr = "";

        swss::FieldValueTuple fvtNoStatus(sai_serialize_object_id(oid), str_attr);

        entries.push_back(fvtNoStatus);
    }

    std::string joined;

    for (const auto &e: entries)
    {
        joined += "||" + fvField(e);
    }

    return str_object_type + ":" + std::to_string(entries.size()) + joined;
}

void test_serialize_bulk_remove_oid(int n, int per)
{
    SWSS_LOG_ENTER();

    auto str = serialize_bulk_vlan(per);
    std::cout << str.substr(0,100) << " ... len " << str.size() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_bulk_vlan(per);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    //std::cout << "ms: " << (double)us.count()/1000 << " n " << n << " x / per " << per << std::endl;
    std::cout << "ms: " << (double)us.count()/1000.0 / n << " n " << n << " / per " << per << std::endl;
}

std::string serialize_bulk_route_entry(int per)
{
    SWSS_LOG_ENTER();

    static auto route_entry = get_route_entry();

    int n = per;

    std::string str_object_type = sai_serialize_object_type(SAI_OBJECT_TYPE_ROUTE_ENTRY);

    std::vector<swss::FieldValueTuple> entries;

    for (int idx = 0; idx < n; ++idx)
    {
        std::string str_attr = "";

        swss::FieldValueTuple fvtNoStatus(sai_serialize_route_entry(route_entry), str_attr);

        entries.push_back(fvtNoStatus);
    }

    std::string joined;

    for (const auto &e: entries)
    {
        joined += "||" + fvField(e);
    }

    return str_object_type + ":" + std::to_string(entries.size()) + joined;
}

void test_serialize_bulk_remove_route_entry(int n, int per)
{
    SWSS_LOG_ENTER();

    auto str = serialize_bulk_route_entry(per);

    std::cout << str.substr(0,100) << " ... len " << str.size() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_bulk_route_entry(per);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    //std::cout << "ms: " << (double)us.count()/1000 << " n " << n << " x / per " << per << std::endl;
    std::cout << "ms: " << (double)us.count()/1000.0 / n << " n " << n << " / per " << per << std::endl;
}

std::string serialize_bulk_fdb_entry(int per)
{
    SWSS_LOG_ENTER();

    static auto fdb_entry = get_fdb_entry();

    int n = per;

    std::string str_object_type = sai_serialize_object_type(SAI_OBJECT_TYPE_FDB_ENTRY);

    std::vector<swss::FieldValueTuple> entries;

    for (int idx = 0; idx < n; ++idx)
    {
        std::string str_attr = "";

        swss::FieldValueTuple fvtNoStatus(sai_serialize_fdb_entry(fdb_entry), str_attr);

        entries.push_back(fvtNoStatus);
    }

    std::string joined;

    for (const auto &e: entries)
    {
        joined += "||" + fvField(e);
    }

    return str_object_type + ":" + std::to_string(entries.size()) + joined;
}

void test_serialize_bulk_remove_fdb_entry(int n, int per)
{
    SWSS_LOG_ENTER();

    auto str = serialize_bulk_fdb_entry(per);

    std::cout << str.substr(0,100) << " ... len " << str.size() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_bulk_fdb_entry(per);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    //std::cout << "ms: " << (double)us.count()/1000 << " n " << n << " x / per " << per << std::endl;
    std::cout << "ms: " << (double)us.count()/1000.0 / n << " n " << n << " / per " << per << std::endl;
}

const std::vector<swss::FieldValueTuple> get_values(const std::vector<std::string>& items)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> values;

    // timestamp|action|objecttype:objectid|attrid=value,...
    for (size_t i = 3; i <items.size(); ++i)
    {
        const std::string& item = items[i];

        auto start = item.find_first_of("=");

        auto field = item.substr(0, start);
        auto value = item.substr(start + 1);

        swss::FieldValueTuple entry(field, value);

        values.push_back(entry);
    }

    return values;
}

SaiAttributeList* get_route_entry_list()
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> values;

    values.push_back(swss::FieldValueTuple("SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION","SAI_PACKET_ACTION_FORWARD"));
    values.push_back(swss::FieldValueTuple("SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID","oid:0x1000000000001"));

    return new SaiAttributeList(SAI_OBJECT_TYPE_ROUTE_ENTRY, values, false);
}

std::string serialize_create_route_entry()
{
    SWSS_LOG_ENTER();

    static auto route_entry = get_route_entry();

    static SaiAttributeList *list = get_route_entry_list();

    std::vector<swss::FieldValueTuple> entry = SaiAttributeList::serialize_attr_list(
            SAI_OBJECT_TYPE_ROUTE_ENTRY,
            list->get_attr_count(),
            list->get_attr_list(),
            false);

    std::string joined;

    for (const auto &e: entry)
    {
        joined += "||" + fvField(e) + "|" + fvValue(e);
    }

    return sai_serialize_object_type(SAI_OBJECT_TYPE_ROUTE_ENTRY) + ":" +
        sai_serialize_route_entry(route_entry) + ":" + joined;
}

void test_serialize_create_route_entry(int n)
{
    SWSS_LOG_ENTER();

    std::cout << serialize_create_route_entry() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_create_route_entry();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    std::cout << "ms: " << (double)us.count()/1000 << " / " << n << std::endl;
}

SaiAttributeList* get_fdb_entry_list()
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> values;

    values.push_back(swss::FieldValueTuple("SAI_FDB_ENTRY_ATTR_TYPE","SAI_FDB_ENTRY_TYPE_DYNAMIC"));
    values.push_back(swss::FieldValueTuple("SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID","oid:0x3a000000000fb"));
    values.push_back(swss::FieldValueTuple("SAI_FDB_ENTRY_ATTR_PACKET_ACTION","SAI_PACKET_ACTION_FORWARD"));

    return new SaiAttributeList(SAI_OBJECT_TYPE_FDB_ENTRY, values, false);
}

std::string serialize_create_fdb_entry()
{
    SWSS_LOG_ENTER();

    static auto fdb_entry = get_fdb_entry();

    static SaiAttributeList *list = get_fdb_entry_list();

    std::vector<swss::FieldValueTuple> entry = SaiAttributeList::serialize_attr_list(
            SAI_OBJECT_TYPE_FDB_ENTRY,
            list->get_attr_count(),
            list->get_attr_list(),
            false);

    std::string joined;

    for (const auto &e: entry)
    {
        joined += "||" + fvField(e) + "|" + fvValue(e);
    }

    return sai_serialize_object_type(SAI_OBJECT_TYPE_FDB_ENTRY) + ":" +
        sai_serialize_fdb_entry(fdb_entry) + ":" + joined;
}

void test_serialize_create_fdb_entry(int n)
{
    SWSS_LOG_ENTER();

    std::cout << serialize_create_fdb_entry() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_create_fdb_entry();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    std::cout << "ms: " << (double)us.count()/1000 << " / " << n << std::endl;
}

SaiAttributeList* get_oid_list()
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> values;

    values.push_back(swss::FieldValueTuple("SAI_VLAN_ATTR_VLAN_ID","2"));

    return new SaiAttributeList(SAI_OBJECT_TYPE_VLAN, values, false);
}

std::string serialize_create_oid()
{
    SWSS_LOG_ENTER();

    static sai_object_id_t oid = 0x123456789abcdef;

    static SaiAttributeList *list = get_oid_list();

    std::vector<swss::FieldValueTuple> entry = SaiAttributeList::serialize_attr_list(
            SAI_OBJECT_TYPE_VLAN,
            list->get_attr_count(),
            list->get_attr_list(),
            false);

    std::string joined;

    for (const auto &e: entry)
    {
        joined += "||" + fvField(e) + "|" + fvValue(e);
    }

    return sai_serialize_object_type(SAI_OBJECT_TYPE_VLAN) + ":" +
        sai_serialize_object_id(oid) + "|" + joined;
}

void test_serialize_create_oid(int n)
{
    SWSS_LOG_ENTER();

    std::cout << serialize_create_oid() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_create_oid();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    std::cout << "ms: " << (double)us.count()/1000 << " / " << n << std::endl;
}

std::string joinFieldValues(
        _In_ const std::vector<swss::FieldValueTuple> &values)
{
    SWSS_LOG_ENTER();

    std::stringstream ss;

    for (size_t i = 0; i < values.size(); ++i)
    {
        const std::string &str_attr_id = fvField(values[i]);
        const std::string &str_attr_value = fvValue(values[i]);

        if(i != 0)
        {
            ss << "|";
        }

        ss << str_attr_id << "=" << str_attr_value;
    }

    return ss.str();
}

std::string serialize_bulk_create_route_entry(int per)
{
    SWSS_LOG_ENTER();

    static auto route_entry = get_route_entry();

    static SaiAttributeList *list = get_route_entry_list();

    std::string str_object_type = sai_serialize_object_type(SAI_OBJECT_TYPE_ROUTE_ENTRY);

    std::vector<swss::FieldValueTuple> entries;

    for (int idx = 0; idx < per; ++idx)
    {
        std::vector<swss::FieldValueTuple> entry =
            SaiAttributeList::serialize_attr_list(SAI_OBJECT_TYPE_ROUTE_ENTRY, list->get_attr_count(), list->get_attr_list(), false);

        std::string str_attr = joinFieldValues(entry);

        std::string str_status = sai_serialize_status(SAI_STATUS_NOT_EXECUTED);

        std::string joined = str_attr + "|" + str_status;

        swss::FieldValueTuple fvt(sai_serialize_route_entry(route_entry) , joined);

        entries.push_back(fvt);
    }

    std::string joined;

    for (const auto &e: entries)
    {
        joined += "||" + fvField(e) + "|" + fvValue(e);
    }

    return str_object_type + ":" + std::to_string(entries.size()) + joined;
}

void test_serialize_bulk_create_route_entry(int n, int per)
{
    SWSS_LOG_ENTER();

    auto str = serialize_bulk_create_route_entry(per);

    std::cout << str.substr(0,300) << " ... len " << str.size() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_bulk_route_entry(per);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    //std::cout << "ms: " << (double)us.count()/1000 << " n " << n << " x / per " << per << std::endl;
    std::cout << "ms: " << (double)us.count()/1000.0 / n << " n " << n << " / per " << per << std::endl;
}

std::string serialize_bulk_create_oid(int per)
{
    SWSS_LOG_ENTER();

    static sai_object_id_t oid = 0x123456789abcdef;

    static SaiAttributeList *list = get_oid_list();

    std::string str_object_type = sai_serialize_object_type(SAI_OBJECT_TYPE_VLAN);

    std::vector<swss::FieldValueTuple> entries;

    for (int idx = 0; idx < per; ++idx)
    {
        std::vector<swss::FieldValueTuple> entry =
            SaiAttributeList::serialize_attr_list(SAI_OBJECT_TYPE_VLAN, list->get_attr_count(), list->get_attr_list(), false);

        std::string str_attr = joinFieldValues(entry);

        std::string str_status = sai_serialize_status(SAI_STATUS_NOT_EXECUTED);

        std::string joined = str_attr + "|" + str_status;

        swss::FieldValueTuple fvt(sai_serialize_object_id(oid) , joined);

        entries.push_back(fvt);
    }

    std::string joined;

    for (const auto &e: entries)
    {
        joined += "||" + fvField(e) + "|" + fvValue(e);
    }

    return str_object_type + ":" + std::to_string(entries.size()) + joined;
}

void test_serialize_bulk_create_oid(int n, int per)
{
    SWSS_LOG_ENTER();

    auto str = serialize_bulk_create_oid(per);

    std::cout << str.substr(0,300) << " ... len " << str.size() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        auto s = serialize_bulk_create_oid(per);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time = end - start;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(time);
    //std::cout << "ms: " << (double)us.count()/1000 << " n " << n << " x / per " << per << std::endl;
    std::cout << "ms: " << (double)us.count()/1000.0 / n << " n " << n << " / per " << per << std::endl;
}

int main()
{
    SWSS_LOG_ENTER();

    std::cout << " * test remove" << std::endl;

    test_serialize_remove_route_entry(10000);
    test_serialize_remove_fdb_entry(10000);
    test_serialize_remove_oid(10000);

    std::cout << " * test bulk remove" << std::endl;

    test_serialize_bulk_remove_route_entry(10,10000);
    test_serialize_bulk_remove_fdb_entry(10,10000);
    test_serialize_bulk_remove_oid(100,10000);

    std::cout << " * test create" << std::endl;

    test_serialize_create_route_entry(10000);
    test_serialize_create_fdb_entry(10000);
    test_serialize_create_oid(10000);

    std::cout << " * test bulk create" << std::endl;

    test_serialize_bulk_create_route_entry(10,10000);
    test_serialize_bulk_create_oid(10,10000);

    return 0;
}
