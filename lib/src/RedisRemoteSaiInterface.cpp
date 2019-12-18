#include "RedisRemoteSaiInterface.h"

#include "sairediscommon.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

#include "swss/select.h"

#include <inttypes.h>

using namespace sairedis;

extern bool g_syncMode;  // TODO make member
extern std::string getSelectResultAsString(int result);

void clear_oid_values(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t attr_count,
        _Out_ sai_attribute_t *attr_list);

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

    clear_oid_values(objectType, attr_count, attr_list);

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

