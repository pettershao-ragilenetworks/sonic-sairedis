#include "sai_redis.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

sai_status_t internal_api_wait_for_response(
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

    SWSS_LOG_INFO("waiting for response %d", api);

    swss::Select s;

    s.addSelectable(g_redisGetConsumer.get());

    while (true)
    {
        SWSS_LOG_INFO("wait for %d api response", api);

        swss::Selectable *sel;

        // get timeout and selector is used for all quad api's
        int result = s.select(&sel, GET_RESPONSE_TIMEOUT);

        if (result == swss::Select::OBJECT)
        {
            swss::KeyOpFieldsValuesTuple kco;

            g_redisGetConsumer->pop(kco);

            const std::string &op = kfvOp(kco);
            const std::string &opkey = kfvKey(kco);

            SWSS_LOG_INFO("response: op = %s, key = %s", opkey.c_str(), op.c_str());

            if (op != "getresponse") // ignore non response messages
            {
                continue;
            }

            sai_status_t status;
            sai_deserialize_status(opkey, status);

            SWSS_LOG_DEBUG("generic %d api status: %d", api, status);

            return status;
        }

        SWSS_LOG_ERROR("generic %d api failed due to SELECT operation result: %s", api, getSelectResultAsString(result).c_str());
        break;
    }

    SWSS_LOG_ERROR("generic %d api failed to get response", api);

    return SAI_STATUS_FAILURE;
}

