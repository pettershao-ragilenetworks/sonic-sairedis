#include "RedisRemoteSaiInterface.h"
#include "Utils.h"

#include "sairediscommon.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

#include "swss/select.h"

#include <inttypes.h>

using namespace sairedis;

extern bool g_syncMode;  // TODO make member
extern std::string getSelectResultAsString(int result);

std::string joinFieldValues(
        _In_ const std::vector<swss::FieldValueTuple> &values);

std::vector<swss::FieldValueTuple> serialize_counter_id_list(
        _In_ const sai_enum_metadata_t *stats_enum,
        _In_ uint32_t count,
        _In_ const sai_stat_id_t *counter_id_list);

RedisRemoteSaiInterface::RedisRemoteSaiInterface(
        _In_ std::shared_ptr<swss::ProducerTable> asicState,
        _In_ std::shared_ptr<swss::ConsumerTable> getConsumer):
    m_asicState(asicState),
    m_getConsumer(getConsumer)
{
    SWSS_LOG_ENTER();

    // empty
}

sai_status_t RedisRemoteSaiInterface::create(
        _In_ sai_object_type_t objectType,
        _Out_ sai_object_id_t* objectId,
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    if (!objectId || *objectId == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_THROW("forgot to allocate object id, FATAL");
    }

    // NOTE: objectId was allocated by the caller

    return create(
            objectType,
            sai_serialize_object_id(*objectId),
            attr_count,
            attr_list);
}

sai_status_t RedisRemoteSaiInterface::remove(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return remove(
            objectType,
            sai_serialize_object_id(objectId));
}

sai_status_t RedisRemoteSaiInterface::set(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    return set(
            objectType,
            sai_serialize_object_id(objectId),
            attr);
}

sai_status_t RedisRemoteSaiInterface::get(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    return get(
            objectType,
            sai_serialize_object_id(objectId),
            attr_count,
            attr_list);
}


#define DECLARE_REMOVE_ENTRY(OT,ot)                             \
sai_status_t RedisRemoteSaiInterface::remove(                   \
        _In_ const sai_ ## ot ## _t* ot)                        \
{                                                               \
    SWSS_LOG_ENTER();                                           \
    return remove(                                              \
            SAI_OBJECT_TYPE_ ## OT,                             \
            sai_serialize_ ## ot(*ot));                         \
}

DECLARE_REMOVE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_REMOVE_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_REMOVE_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_REMOVE_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_REMOVE_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_REMOVE_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_REMOVE_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_REMOVE_ENTRY(NAT_ENTRY,nat_entry);

#define DECLARE_CREATE_ENTRY(OT,ot)                             \
sai_status_t RedisRemoteSaiInterface::create(                   \
        _In_ const sai_ ## ot ## _t* ot,                        \
        _In_ uint32_t attr_count,                               \
        _In_ const sai_attribute_t *attr_list)                  \
{                                                               \
    SWSS_LOG_ENTER();                                           \
    return create(                                              \
            SAI_OBJECT_TYPE_ ## OT,                             \
            sai_serialize_ ## ot(*ot),                          \
            attr_count,                                         \
            attr_list);                                         \
}

DECLARE_CREATE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_CREATE_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_CREATE_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_CREATE_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_CREATE_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_CREATE_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_CREATE_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_CREATE_ENTRY(NAT_ENTRY,nat_entry);

#define DECLARE_SET_ENTRY(OT,ot)                                \
sai_status_t RedisRemoteSaiInterface::set(                      \
        _In_ const sai_ ## ot ## _t* ot,                        \
        _In_ const sai_attribute_t *attr)                       \
{                                                               \
    SWSS_LOG_ENTER();                                           \
    return set(                                                 \
            SAI_OBJECT_TYPE_ ## OT,                             \
            sai_serialize_ ## ot(*ot),                          \
            attr);                                              \
}

DECLARE_SET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_SET_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_SET_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_SET_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_SET_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_SET_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_SET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_SET_ENTRY(NAT_ENTRY,nat_entry);

sai_status_t RedisRemoteSaiInterface::create(
        _In_ sai_object_type_t object_type,
        _In_ const std::string& serializedObjectId,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> entry = SaiAttributeList::serialize_attr_list(
            object_type,
            attr_count,
            attr_list,
            false);

    if (entry.size() == 0)
    {
        // make sure that we put object into db
        // even if there are no attributes set
        swss::FieldValueTuple null("NULL", "NULL");

        entry.push_back(null);
    }

    auto serializedObjectType = sai_serialize_object_type(object_type);

    const std::string key = serializedObjectType + ":" + serializedObjectId;

    SWSS_LOG_DEBUG("generic create key: %s, fields: %" PRIu64, key.c_str(), entry.size());

    m_asicState->set(key, entry, REDIS_ASIC_STATE_COMMAND_CREATE);

    return waitForResponse(SAI_COMMON_API_CREATE);
}

sai_status_t RedisRemoteSaiInterface::remove(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& serializedObjectId)
{
    SWSS_LOG_ENTER();

    auto serializedObjectType = sai_serialize_object_type(objectType);

    const std::string key = serializedObjectType + ":" + serializedObjectId;

    SWSS_LOG_DEBUG("generic remove key: %s", key.c_str());

    m_asicState->del(key, REDIS_ASIC_STATE_COMMAND_REMOVE);

    return waitForResponse(SAI_COMMON_API_REMOVE);
}

sai_status_t RedisRemoteSaiInterface::set(
        _In_ sai_object_type_t objectType,
        _In_ const std::string &serializedObjectId,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> entry = SaiAttributeList::serialize_attr_list(
            objectType,
            1,
            attr,
            false);

    auto serializedObjectType = sai_serialize_object_type(objectType);

    std::string key = serializedObjectType + ":" + serializedObjectId;

    SWSS_LOG_DEBUG("generic set key: %s, fields: %lu", key.c_str(), entry.size());

    m_asicState->set(key, entry, REDIS_ASIC_STATE_COMMAND_SET);

    return waitForResponse(SAI_COMMON_API_SET);
}

sai_status_t RedisRemoteSaiInterface::waitForResponse(
        _In_ sai_common_api_t api)
{
    SWSS_LOG_ENTER();

    if (!g_syncMode)
    {
        /*
         * By default sync mode is disabled and all create/set/remove are
         * considered success operations.
         */

        return SAI_STATUS_SUCCESS;
    }

    auto strApi = sai_serialize_common_api(api);

    swss::Select s;

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_INFO("wait for %s response", strApi.c_str());

        swss::Selectable *sel;

        int result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &op = kfvOp(kco);
            const std::string &opkey = kfvKey(kco);

            SWSS_LOG_INFO("response: op = %s, key = %s", opkey.c_str(), op.c_str());

            if (op != REDIS_ASIC_STATE_COMMAND_GETRESPONSE)
            {
                // ignore non response messages
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(opkey, status);

            SWSS_LOG_DEBUG("generic %s status: %s", strApi.c_str(), opkey.c_str());

            return status;
        }

        SWSS_LOG_ERROR("generic %d api failed due to SELECT operation result: %s", api, getSelectResultAsString(result).c_str());
        break;
    }

    SWSS_LOG_ERROR("generic %s failed to get response", strApi.c_str());

    return SAI_STATUS_FAILURE;
}

sai_status_t RedisRemoteSaiInterface::waitForGetResponse(
        _In_ sai_object_type_t objectType,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    // NOTE since the same channel is used by all QUAD api part of this
    // function can be combined with waitForResponse and extra work would be
    // needed to process results

    swss::Select s;

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_DEBUG("wait for response");

        swss::Selectable *sel;

        int result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &op = kfvOp(kco);
            const std::string &opkey = kfvKey(kco);
            const auto& values = kfvFieldsValues(kco);

            SWSS_LOG_INFO("response: op = %s, key = %s", opkey.c_str(), op.c_str());

            if (op != REDIS_ASIC_STATE_COMMAND_GETRESPONSE) // ignore non response messages
            {
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(opkey, status);

            // we could deserialize directly to user data, but list is
            // allocated by deserializer we would need another method for that

            if (status == SAI_STATUS_SUCCESS)
            {
                SaiAttributeList list(objectType, values, false);

                transfer_attributes(objectType, attr_count, list.get_attr_list(), attr_list, false);
            }
            else if (status == SAI_STATUS_BUFFER_OVERFLOW)
            {
                SaiAttributeList list(objectType, values, true);

                // no need for id fix since this is overflow
                transfer_attributes(objectType, attr_count, list.get_attr_list(), attr_list, true);
            }

            SWSS_LOG_DEBUG("generic get status: %d", status);

            return status;
        }

        SWSS_LOG_ERROR("generic get failed due to SELECT operation result: %s", getSelectResultAsString(result).c_str());
        break;
    }

    SWSS_LOG_ERROR("generic get failed to get response");

    return SAI_STATUS_FAILURE;
}

sai_status_t RedisRemoteSaiInterface::get(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& serializedObjectId,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    Utils::clearOidValues(objectType, attr_count, attr_list);

    std::vector<swss::FieldValueTuple> entry = SaiAttributeList::serialize_attr_list(
            objectType,
            attr_count,
            attr_list,
            false);

    std::string serializedObjectType = sai_serialize_object_type(objectType);

    std::string key = serializedObjectType + ":" + serializedObjectId;

    SWSS_LOG_DEBUG("generic get key: %s, fields: %lu", key.c_str(), entry.size());

    // get is special, it will not put data
    // into asic view, only to message queue
    m_asicState->set(key, entry, REDIS_ASIC_STATE_COMMAND_GET);

    return waitForGetResponse(objectType, attr_count, attr_list);
}

#define DECLARE_GET_ENTRY(OT,ot)                                \
sai_status_t RedisRemoteSaiInterface::get(                      \
        _In_ const sai_ ## ot ## _t* ot,                        \
        _In_ uint32_t attr_count,                               \
        _Inout_ sai_attribute_t *attr_list)                     \
{                                                               \
    SWSS_LOG_ENTER();                                           \
    return get(                                                 \
            SAI_OBJECT_TYPE_ ## OT,                             \
            sai_serialize_ ## ot(*ot),                          \
            attr_count,                                         \
            attr_list);                                         \
}

DECLARE_GET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_GET_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_GET_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_GET_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_GET_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_GET_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_GET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_GET_ENTRY(NAT_ENTRY,nat_entry);

sai_status_t RedisRemoteSaiInterface::waitForFlushFdbEntriesResponse()
{
    SWSS_LOG_ENTER();

    swss::Select s;

    // get consumer will be reused for flush

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_DEBUG("wait for response");

        swss::Selectable *sel;

        int result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &op = kfvOp(kco);
            const std::string &opkey = kfvKey(kco);

            SWSS_LOG_DEBUG("response: op = %s, key = %s", opkey.c_str(), op.c_str());

            if (op != REDIS_ASIC_STATE_COMMAND_FLUSHRESPONSE) // ignore non response messages
            {
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(opkey, status);

            SWSS_LOG_NOTICE("flush status: %s", opkey.c_str());

            return status;
        }

        SWSS_LOG_ERROR("flush failed due to SELECT operation result: %s", getSelectResultAsString(result).c_str());
        break;
    }

    SWSS_LOG_ERROR("flush failed to get response");

    return SAI_STATUS_FAILURE;
}

sai_status_t RedisRemoteSaiInterface::flushFdbEntries(
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> entry = SaiAttributeList::serialize_attr_list(
            SAI_OBJECT_TYPE_FDB_FLUSH,
            attrCount,
            attrList,
            false);

    std::string serializedObjectId = sai_serialize_object_type(SAI_OBJECT_TYPE_FDB_FLUSH);

    // NOTE ! we actually give switch ID since FLUSH is not real object
    std::string key = serializedObjectId + ":" + sai_serialize_object_id(switchId);

    SWSS_LOG_NOTICE("flush key: %s, fields: %lu", key.c_str(), entry.size());

    m_asicState->set(key, entry, REDIS_ASIC_STATE_COMMAND_FLUSH);

    return waitForFlushFdbEntriesResponse();
}

sai_status_t RedisRemoteSaiInterface::objectTypeGetAvailability(
        _In_ sai_object_id_t switchId,
        _In_ sai_object_type_t objectType,
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList,
        _Out_ uint64_t *count)
{
    SWSS_LOG_ENTER();

    const std::string switch_id_str = sai_serialize_object_id(switchId);
    const std::string object_type_str = sai_serialize_object_type(objectType);

    std::vector<swss::FieldValueTuple> query_arguments =
        SaiAttributeList::serialize_attr_list(
                objectType,
                attrCount,
                attrList,
                false);

    SWSS_LOG_DEBUG(
            "Query arguments: switch: %s, object type: %s, attributes: %s",
            switch_id_str.c_str(),
            object_type_str.c_str(),
            joinFieldValues(query_arguments).c_str()
    );

    // Syncd will pop this argument off before trying to deserialize the attribute list
    query_arguments.push_back(swss::FieldValueTuple("OBJECT_TYPE", object_type_str));

    // This query will not put any data into the ASIC view, just into the
    // message queue
    m_asicState->set(switch_id_str, query_arguments, REDIS_ASIC_STATE_COMMAND_OBJECT_TYPE_GET_AVAILABILITY_QUERY);

    return waitForObjectTypeGetAvailabilityResponse(count);
}

sai_status_t RedisRemoteSaiInterface::waitForObjectTypeGetAvailabilityResponse(
        _Inout_ uint64_t *count)
{
    SWSS_LOG_ENTER();

    // TODO could be combined with the rest methods

    swss::Select s;

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_DEBUG("Waiting for a response");

        swss::Selectable *sel;

        auto result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &message_type = kfvOp(kco);
            const std::string &status_str = kfvKey(kco);

            SWSS_LOG_DEBUG("Received response: op = %s, key = %s", message_type.c_str(), status_str.c_str());

            // Ignore messages that are not in response to our query
            if (message_type != REDIS_ASIC_STATE_COMMAND_OBJECT_TYPE_GET_AVAILABILITY_RESPONSE)
            {
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(status_str, status);

            if (status == SAI_STATUS_SUCCESS)
            {
                const std::vector<swss::FieldValueTuple> &values = kfvFieldsValues(kco);

                if (values.size() != 1)
                {
                    SWSS_LOG_ERROR("Invalid response from syncd: expected 1 value, received %zu", values.size());
                    return SAI_STATUS_FAILURE;
                }

                const std::string &availability_str = fvValue(values[0]);
                *count = std::stol(availability_str);

                SWSS_LOG_DEBUG("Received payload: count = %lu", *count);
            }
            else

            SWSS_LOG_DEBUG("Status: %s", status_str.c_str());
            return status;
        }
    }

    SWSS_LOG_ERROR("Failed to receive a response from syncd");

    return SAI_STATUS_FAILURE;
}

sai_status_t RedisRemoteSaiInterface::queryAattributeEnumValuesCapability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ sai_attr_id_t attr_id,
        _Inout_ sai_s32_list_t *enum_values_capability)
{
    SWSS_LOG_ENTER();

    const std::string switch_id_str = sai_serialize_object_id(switch_id);
    const std::string object_type_str = sai_serialize_object_type(object_type);

    auto meta = sai_metadata_get_attr_metadata(object_type, attr_id);

    if (meta == NULL)
    {
        SWSS_LOG_ERROR("Failed to find attribute metadata: object type %s, attr id %d", object_type_str.c_str(), attr_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    const std::string attr_id_str = sai_serialize_attr_id(*meta);
    const std::string list_size = std::to_string(enum_values_capability->count);

    const std::vector<swss::FieldValueTuple> query_arguments =
    {
        swss::FieldValueTuple("OBJECT_TYPE", object_type_str),
        swss::FieldValueTuple("ATTR_ID", attr_id_str),
        swss::FieldValueTuple("LIST_SIZE", list_size)
    };

    SWSS_LOG_DEBUG(
            "Query arguments: switch %s, object type: %s, attribute: %s, count: %s",
            switch_id_str.c_str(),
            object_type_str.c_str(),
            attr_id_str.c_str(),
            list_size.c_str()
    );

    // This query will not put any data into the ASIC view, just into the
    // message queue

    m_asicState->set(switch_id_str, query_arguments, REDIS_ASIC_STATE_COMMAND_ATTR_ENUM_VALUES_CAPABILITY_QUERY);

    return waitForQueryAattributeEnumValuesCapabilityResponse(enum_values_capability);
}

sai_status_t RedisRemoteSaiInterface::waitForQueryAattributeEnumValuesCapabilityResponse(
        _Inout_ sai_s32_list_t* enumValuesCapability)
{
    SWSS_LOG_ENTER();

    swss::Select s;

    // TODO could be unified to get response

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_DEBUG("Waiting for a response");

        swss::Selectable *sel;

        auto result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &message_type = kfvOp(kco);
            const std::string &status_str = kfvKey(kco);

            SWSS_LOG_DEBUG("Received response: op = %s, key = %s", message_type.c_str(), status_str.c_str());

            // Ignore messages that are not in response to our query
            if (message_type != REDIS_ASIC_STATE_COMMAND_ATTR_ENUM_VALUES_CAPABILITY_RESPONSE)
            {
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(status_str, status);

            if (status == SAI_STATUS_SUCCESS)
            {
                const std::vector<swss::FieldValueTuple> &values = kfvFieldsValues(kco);

                if (values.size() != 2)
                {
                    SWSS_LOG_ERROR("Invalid response from syncd: expected 2 values, received %zu", values.size());

                    return SAI_STATUS_FAILURE;
                }

                const std::string &capability_str = fvValue(values[0]);
                const uint32_t num_capabilities = std::stoi(fvValue(values[1]));

                SWSS_LOG_DEBUG("Received payload: capabilites = '%s', count = %d", capability_str.c_str(), num_capabilities);

                enumValuesCapability->count = num_capabilities;

                size_t position = 0;
                for (uint32_t i = 0; i < num_capabilities; i++)
                {
                    size_t old_position = position;
                    position = capability_str.find(",", old_position);
                    std::string capability = capability_str.substr(old_position, position - old_position);
                    enumValuesCapability->list[i] = std::stoi(capability);

                    // We have run out of values to add to our list
                    if (position == std::string::npos)
                    {
                        if (num_capabilities != i + 1)
                        {
                            SWSS_LOG_WARN("Query returned less attributes than expected: expected %d, recieved %d", num_capabilities, i+1);
                        }

                        break;
                    }

                    // Skip the commas
                    position++;
                }
            }
            else if (status ==  SAI_STATUS_BUFFER_OVERFLOW)
            {
                // TODO on sai status overflow we should populate correct count on the list

                SWSS_LOG_ERROR("TODO need to handle SAI_STATUS_BUFFER_OVERFLOW, FIXME");
            }

            SWSS_LOG_DEBUG("Status: %s", status_str.c_str());

            return status;
        }
    }

    SWSS_LOG_ERROR("Failed to receive a response from syncd");

    return SAI_STATUS_FAILURE;
}

sai_status_t RedisRemoteSaiInterface::getStats(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids,
        _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    auto stats_enum = sai_metadata_get_object_type_info(object_type)->statenum;

    auto entry = serialize_counter_id_list(stats_enum, number_of_counters, counter_ids);

    std::string str_object_type = sai_serialize_object_type(object_type);

    std::string key = str_object_type + ":" + sai_serialize_object_id(object_id);

    SWSS_LOG_DEBUG("generic get stats key: %s, fields: %lu", key.c_str(), entry.size());

    // get_stats will not put data to asic view, only to message queue

    m_asicState->set(key, entry, REDIS_ASIC_STATE_COMMAND_GET_STATS);

    return waitForGetStatsResponse(number_of_counters, counters);
}

sai_status_t RedisRemoteSaiInterface::waitForGetStatsResponse(
        _In_ uint32_t number_of_counters,
        _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    // wait for response

    swss::Select s;

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_DEBUG("wait for get_stats response");

        swss::Selectable *sel;

        int result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &opkey = kfvKey(kco);
            const std::string &op = kfvOp(kco);

            SWSS_LOG_DEBUG("response: op = %s, key = %s", opkey.c_str(), op.c_str());

            if (op != REDIS_ASIC_STATE_COMMAND_GETRESPONSE) // ignore non response messages
            {
                continue;
            }

            // key:         sai_status
            // field:       stat_id
            // value:       stat_value

            auto &values = kfvFieldsValues(kco);

            sai_status_t status;
            sai_deserialize_status(opkey, status);

            if (status == SAI_STATUS_SUCCESS)
            {
                if (values.size () != number_of_counters)
                {
                    SWSS_LOG_THROW("wrong number of counters, got %zu, expected %u", values.size(), number_of_counters);
                }

                for (uint32_t idx = 0; idx < number_of_counters; idx++)
                {
                    counters[idx] = stoull(fvValue(values[idx]));
                }
            }

            SWSS_LOG_DEBUG("generic get status: %s", sai_serialize_object_id(status).c_str());

            return status;
        }

        SWSS_LOG_ERROR("generic get failed due to SELECT operation result");
        break;
    }

    SWSS_LOG_ERROR("generic get stats failed to get response");

    return SAI_STATUS_FAILURE;
}

sai_status_t RedisRemoteSaiInterface::getStatsExt(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids,
        _In_ sai_stats_mode_t mode,
        _Out_ uint64_t *counters)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_ERROR("not implemented");

    // TODO could be the same as getStats but put mode at first argument

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t RedisRemoteSaiInterface::clearStats(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t number_of_counters,
        _In_ const sai_stat_id_t *counter_ids)
{
    SWSS_LOG_ENTER();

    auto stats_enum = sai_metadata_get_object_type_info(object_type)->statenum;

    auto values = serialize_counter_id_list(stats_enum, number_of_counters, counter_ids);

    auto str_object_type = sai_serialize_object_type(object_type);

    auto key = str_object_type + ":" + sai_serialize_object_id(object_id);

    SWSS_LOG_DEBUG("generic clear stats key: %s, fields: %lu", key.c_str(), values.size());

    // clear_stats will not put data into asic view, only to message queue

    m_asicState->set(key, values, REDIS_ASIC_STATE_COMMAND_CLEAR_STATS);

    return waitForClearStatsResponse();
}

sai_status_t RedisRemoteSaiInterface::waitForClearStatsResponse()
{
    SWSS_LOG_ENTER();

    // wait for response

    swss::Select s;

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_DEBUG("wait for clear_stats response");

        swss::Selectable *sel;

        int result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &respKey = kfvKey(kco);
            const std::string &respOp = kfvOp(kco);

            SWSS_LOG_DEBUG("response: key = %s, op = %s", respKey.c_str(), respOp.c_str());

            if (respOp != REDIS_ASIC_STATE_COMMAND_GETRESPONSE) // ignore non response messages
            {
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(respKey, status);

            SWSS_LOG_DEBUG("generic clear stats status: %s", sai_serialize_status(status).c_str());

            return status;
        }

        SWSS_LOG_ERROR("generic clear stats failed due to SELECT operation result");
        break;
    }

    SWSS_LOG_ERROR("generic clear stats failed to get response");

    return SAI_STATUS_FAILURE;
}

sai_status_t RedisRemoteSaiInterface::bulkRemove(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode, this will need to go as extra parameter and needs to
    // be supported by LUA script passed as first or last entry in values,
    // currently mode is ignored

    std::string serializedObjectType = sai_serialize_object_type(object_type);

    std::vector<swss::FieldValueTuple> entries;

    for (size_t idx = 0; idx < serialized_object_ids.size(); ++idx)
    {
        std::string str_attr = "";

        swss::FieldValueTuple fvtNoStatus(serialized_object_ids[idx], str_attr);

        entries.push_back(fvtNoStatus);
    }

    /*
     * We are adding number of entries to actually add ':' to be compatible
     * with previous
     */

    // key:         object_type:count
    // field:       object_id
    // value:       object_attrs
    std::string key = serializedObjectType + ":" + std::to_string(entries.size());

    m_asicState->set(key, entries, REDIS_ASIC_STATE_COMMAND_BULK_REMOVE);

    return waitForBulkResponse(SAI_COMMON_API_BULK_REMOVE, (uint32_t)serialized_object_ids.size(), object_statuses);
}

sai_status_t RedisRemoteSaiInterface::waitForBulkResponse(
        _In_ sai_common_api_t api,
        _In_ uint32_t object_count,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    if (!g_syncMode)
    {
        /*
         * By default sync mode is disabled and all bulk create/set/remove are
         * considered success operations.
         */

        for (uint32_t idx = 0; idx < object_count; idx++)
        {
            object_statuses[idx] = SAI_STATUS_SUCCESS;
        }

        return SAI_STATUS_SUCCESS;
    }

    // similar to waitForResponse

    auto strApi = sai_serialize_common_api(api);

    swss::Select s;

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_INFO("wait for %s response", strApi.c_str());

        swss::Selectable *sel;

        int result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &op = kfvOp(kco);
            const std::string &opkey = kfvKey(kco);
            auto &values = kfvFieldsValues(kco);

            SWSS_LOG_INFO("response: op = %s, key = %s", opkey.c_str(), op.c_str());

            if (op != REDIS_ASIC_STATE_COMMAND_GETRESPONSE)
            {
                // ignore non response messages
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(opkey, status);

            SWSS_LOG_DEBUG("generic %s status: %s", strApi.c_str(), opkey.c_str());

            if (values.size () != object_count)
            {
                SWSS_LOG_THROW("wrong number of counters, got %zu, expected %u", values.size(), object_count);
            }

            // deserialize statuses for all objects

            for (uint32_t idx = 0; idx < object_count; idx++)
            {
                sai_deserialize_status(fvField(values[idx]), object_statuses[idx]);
            }

            return status;
        }

        SWSS_LOG_ERROR("generic %d api failed due to SELECT operation result: %s", api, getSelectResultAsString(result).c_str());
        break;
    }

    SWSS_LOG_ERROR("generic %s failed to get response", strApi.c_str());

    return SAI_STATUS_FAILURE;
}

sai_status_t RedisRemoteSaiInterface::bulkRemove(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t object_count,
        _In_ const sai_object_id_t *object_id,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_object_id(object_id[idx]));
    }

    return bulkRemove(object_type, serializedObjectIds, mode, object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkRemove(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t *route_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_route_entry(route_entry[idx]));
    }

    return bulkRemove(SAI_OBJECT_TYPE_ROUTE_ENTRY, serializedObjectIds, mode, object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkRemove(
        _In_ uint32_t object_count,
        _In_ const sai_nat_entry_t *nat_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_nat_entry(nat_entry[idx]));
    }

    return bulkRemove(SAI_OBJECT_TYPE_NAT_ENTRY, serializedObjectIds, mode, object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkRemove(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t *fdb_entry,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_fdb_entry(fdb_entry[idx]));
    }

    return bulkRemove(SAI_OBJECT_TYPE_FDB_ENTRY, serializedObjectIds, mode, object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkSet(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t object_count,
        _In_ const sai_object_id_t *object_id,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_object_id(object_id[idx]));
    }

    return bulkSet(object_type, serializedObjectIds, attr_list, mode, object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkSet(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t *route_entry,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_route_entry(route_entry[idx]));
    }

    return bulkSet(SAI_OBJECT_TYPE_ROUTE_ENTRY, serializedObjectIds, attr_list, mode, object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkSet(
        _In_ uint32_t object_count,
        _In_ const sai_nat_entry_t *nat_entry,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_nat_entry(nat_entry[idx]));
    }

    return bulkSet(SAI_OBJECT_TYPE_NAT_ENTRY, serializedObjectIds, attr_list, mode, object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkSet(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t *fdb_entry,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    std::vector<std::string> serializedObjectIds;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        serializedObjectIds.emplace_back(sai_serialize_fdb_entry(fdb_entry[idx]));
    }

    return bulkSet(SAI_OBJECT_TYPE_FDB_ENTRY, serializedObjectIds, attr_list, mode, object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkSet(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<swss::FieldValueTuple> entries;

    for (size_t idx = 0; idx < serialized_object_ids.size(); ++idx)
    {
        auto entry = SaiAttributeList::serialize_attr_list(object_type, 1, &attr_list[idx], false);

        std::string str_attr = joinFieldValues(entry);

        swss::FieldValueTuple value(serialized_object_ids[idx], str_attr);

        entries.push_back(value);
    }

    /*
     * We are adding number of entries to actually add ':' to be compatible
     * with previous
     */

    std::string key = sai_serialize_object_type(object_type) + ":" + std::to_string(entries.size());

    m_asicState->set(key, entries, REDIS_ASIC_STATE_COMMAND_BULK_SET);

    return waitForBulkResponse(SAI_COMMON_API_BULK_SET, (uint32_t)serialized_object_ids.size(), object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkCreate(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t object_count,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_object_id_t *object_id,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_object_id(object_id[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return bulkCreate(
            object_type,
            serialized_object_ids,
            attr_count,
            attr_list,
            mode,
            object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkCreate(
        _In_ sai_object_type_t object_type,
        _In_ const std::vector<std::string> &serialized_object_ids,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Inout_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::string str_object_type = sai_serialize_object_type(object_type);

    std::vector<swss::FieldValueTuple> entries;

    for (size_t idx = 0; idx < serialized_object_ids.size(); ++idx)
    {
        auto entry = SaiAttributeList::serialize_attr_list(object_type, attr_count[idx], attr_list[idx], false);

        std::string str_attr = joinFieldValues(entry);

        swss::FieldValueTuple fvtNoStatus(serialized_object_ids[idx] , str_attr);

        entries.push_back(fvtNoStatus);
    }

    /*
     * We are adding number of entries to actually add ':' to be compatible
     * with previous
     */

    // key:         object_type:count
    // field:       object_id
    // value:       object_attrs
    std::string key = str_object_type + ":" + std::to_string(entries.size());

    m_asicState->set(key, entries, REDIS_ASIC_STATE_COMMAND_BULK_CREATE);

    return waitForBulkResponse(SAI_COMMON_API_BULK_CREATE, (uint32_t)serialized_object_ids.size(), object_statuses);
}

sai_status_t RedisRemoteSaiInterface::bulkCreate(
        _In_ uint32_t object_count,
        _In_ const sai_route_entry_t* route_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_route_entry(route_entry[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return bulkCreate(
            SAI_OBJECT_TYPE_ROUTE_ENTRY,
            serialized_object_ids,
            attr_count,
            attr_list,
            mode,
            object_statuses);
}


sai_status_t RedisRemoteSaiInterface::bulkCreate(
        _In_ uint32_t object_count,
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_fdb_entry(fdb_entry[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return bulkCreate(
            SAI_OBJECT_TYPE_FDB_ENTRY,
            serialized_object_ids,
            attr_count,
            attr_list,
            mode,
            object_statuses);
}


sai_status_t RedisRemoteSaiInterface::bulkCreate(
        _In_ uint32_t object_count,
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ const uint32_t *attr_count,
        _In_ const sai_attribute_t **attr_list,
        _In_ sai_bulk_op_error_mode_t mode,
        _Out_ sai_status_t *object_statuses)
{
    SWSS_LOG_ENTER();

    // TODO support mode

    std::vector<std::string> serialized_object_ids;

    // on create vid is put in db by syncd
    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        std::string str_object_id = sai_serialize_nat_entry(nat_entry[idx]);
        serialized_object_ids.push_back(str_object_id);
    }

    return bulkCreate(
            SAI_OBJECT_TYPE_NAT_ENTRY,
            serialized_object_ids,
            attr_count,
            attr_list,
            mode,
            object_statuses);
}

std::string RedisRemoteSaiInterface::getSelectResultAsString(int result)
{
    SWSS_LOG_ENTER();

    std::string res;

    switch (result)
    {
        case swss::Select::ERROR:
            res = "ERROR";
            break;

        case swss::Select::TIMEOUT:
            res = "TIMEOUT";
            break;

        default:
            SWSS_LOG_WARN("non recognized select result: %d", result);
            res = std::to_string(result);
            break;
    }

    return res;
}

sai_status_t RedisRemoteSaiInterface::notifySyncd(
        _In_ sai_object_id_t switchId,
        _In_ sai_redis_notify_syncd_t redisNotifySyncd)
{
    SWSS_LOG_ENTER();

    std::vector<swss::FieldValueTuple> entry;

    std::string key;

    switch(redisNotifySyncd)
    {
        // TODO move this validation to metadata remote

        case SAI_REDIS_NOTIFY_SYNCD_INIT_VIEW:

            SWSS_LOG_NOTICE("sending syncd INIT view");
            key = SYNCD_INIT_VIEW;
            break;

        case SAI_REDIS_NOTIFY_SYNCD_APPLY_VIEW:

            SWSS_LOG_NOTICE("sending syncd APPLY view");
            key = SYNCD_APPLY_VIEW;
            break;

        case SAI_REDIS_NOTIFY_SYNCD_INSPECT_ASIC:

            SWSS_LOG_NOTICE("sending syncd INSPECT ASIC");
            key = SYNCD_INSPECT_ASIC;
            break;

        default:

            SWSS_LOG_ERROR("invalid notify syncd attr value %d", redisNotifySyncd);

            return SAI_STATUS_FAILURE;
    }

    // we need to use "GET" channel to be sure that
    // all previous operations were applied, if we don't
    // use GET channel then we may hit race condition
    // on syncd side where syncd will start compare view
    // when there are still objects in op queue
    //
    // other solution can be to use notify event
    // and then on syncd side read all the asic state queue
    // and apply changes before switching to init/apply mode

    m_asicState->set(key, entry, REDIS_ASIC_STATE_COMMAND_NOTIFY);

    return waitForNotifySyncdResponse();
}

sai_status_t RedisRemoteSaiInterface::waitForNotifySyncdResponse()
{
    SWSS_LOG_ENTER();

    swss::Select s;

    s.addSelectable(m_getConsumer.get());

    while (true)
    {
        SWSS_LOG_NOTICE("wait for notify response");

        swss::Selectable *sel;

        int result = s.select(&sel, REDIS_ASIC_STATE_COMMAND_GETRESPONSE_TIMEOUT_MS);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            m_getConsumer->pop(kco);

            const std::string &op = kfvOp(kco);
            const std::string &opkey = kfvKey(kco);

            SWSS_LOG_NOTICE("notify response: %s", opkey.c_str());

            if (op != REDIS_ASIC_STATE_COMMAND_NOTIFY) // response is the same name as query
            {
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(opkey, status);

            return status;
        }

        SWSS_LOG_ERROR("notify syncd failed to get response result from select: %s", getSelectResultAsString(result).c_str());
        break;
    }

    SWSS_LOG_ERROR("notify syncd failed to get response");

    return SAI_STATUS_FAILURE;
}

