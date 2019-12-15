#include "RedisRemoteSaiInterface.h"

#include "sairediscommon.h"
#include "meta/sai_serialize.h"

#include "swss/select.h"

using namespace sairedis;

extern bool g_syncMode;  // TODO make member
extern std::string getSelectResultAsString(int result);

RedisRemoteSaiInterface::RedisRemoteSaiInterface(
        _In_ std::shared_ptr<swss::ProducerTable> asicState,
        _In_ std::shared_ptr<swss::ConsumerTable> getConsumer):
    m_asicState(asicState),
    m_getConsumer(getConsumer)
{
    SWSS_LOG_ENTER();

    // empty
}

sai_status_t RedisRemoteSaiInterface::remove(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return remove(
            sai_serialize_object_type(objectType),
            sai_serialize_object_id(objectId));
}

sai_status_t RedisRemoteSaiInterface::remove(
        _In_ const std::string& serializedObjectType,
        _In_ const std::string& serializedObjectId)
{
    SWSS_LOG_ENTER();

    const std::string key = serializedObjectType + ":" + serializedObjectId;

    SWSS_LOG_DEBUG("generic remove key: %s", key.c_str());

    m_asicState->del(key, REDIS_ASIC_STATE_COMMAND_REMOVE);

    return waitForResponse(SAI_COMMON_API_REMOVE);
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
