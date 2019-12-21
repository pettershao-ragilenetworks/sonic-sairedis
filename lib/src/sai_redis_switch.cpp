#include "sai_redis.h"
#include "sairedis.h"
#include "sairediscommon.h"

#include "meta/sai_serialize.h"

#include <thread>

using namespace sairedis;

volatile bool g_asicInitViewMode = false; // default mode is apply mode
volatile bool g_useTempView = false;
volatile bool g_syncMode = false;

static sai_status_t sai_redis_notify_syncd(
        _In_ sai_object_id_t switchId,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    auto redisNotifySyncd = (sai_redis_notify_syncd_t)attr->value.s32;

    auto status = g_remoteSaiInterface->notifySyncd(switchId, redisNotifySyncd);

    if (status == SAI_STATUS_SUCCESS)
    {
        switch (redisNotifySyncd)
        {
            case SAI_REDIS_NOTIFY_SYNCD_INIT_VIEW:

                SWSS_LOG_NOTICE("switched ASIC to INIT VIEW");

                g_asicInitViewMode = true;

                SWSS_LOG_NOTICE("clearing current local state since init view is called on initialized switch");

                // TODO this must be per syncd instance
                clear_local_state();

                break;

            case SAI_REDIS_NOTIFY_SYNCD_APPLY_VIEW:

                SWSS_LOG_NOTICE("switched ASIC to APPLY VIEW");

                g_asicInitViewMode = false;

                break;

            default:
                break;
        }
    }

    return status;
}

/*
 * NOTE: Notifications during switch create and switch remove.
 *
 * It is possible that when we create switch we will immediately start getting
 * notifications from it, and it may happen that this switch will not be yet
 * put to switch container and notification won't find it. But before
 * notification will be processed it will first try to acquire mutex, so create
 * switch function will end and switch will be put inside container.
 *
 * Similar it can happen that we receive notification when we are removing
 * switch, then switch will be removed from switch container and notification
 * will not find existing switch, but that's ok since switch was removed, and
 * notification can be ignored.
 */

static sai_status_t redis_create_switch(
        _Out_ sai_object_id_t* switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return g_meta->create(
            SAI_OBJECT_TYPE_SWITCH,
            switch_id,
            SAI_NULL_OBJECT_ID, // no switch id since we create switch
            attr_count,
            attr_list,
            *g_remoteSaiInterface);
}

static sai_status_t redis_set_switch_attribute(
        _In_ sai_object_id_t switch_id,
        _In_ const sai_attribute_t *attr)
{
    // SWSS_LOG_ENTER() omitted here, defined below after mutex

    if (attr != NULL && attr->id == SAI_REDIS_SWITCH_ATTR_PERFORM_LOG_ROTATE)
    {
        /*
         * Let's avoid using mutexes, since this attribute could be used in
         * signal handler, so check it's value here. If set this attribute will
         * be performed from multiple threads there is possibility for race
         * condition here, but this doesn't matter since we only set logrotate
         * flag, and if that happens we will just reopen file less times then
         * actual set operation was called.
         */

        auto rec = g_recorder; // make local to keep reference

        if (rec)
        {
            rec->requestLogRotate();
        }

        return SAI_STATUS_SUCCESS;
    }

    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    if (attr != NULL)
    {
        /*
         * NOTE: that this will work without
         * switch being created.
         */

        switch (attr->id)
        {
            case SAI_REDIS_SWITCH_ATTR_PERFORM_LOG_ROTATE:
                if (g_recorder)
                    g_recorder->requestLogRotate();
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_RECORD:
                if (g_recorder)
                    g_recorder->enableRecording(attr->value.booldata);
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_NOTIFY_SYNCD:
                return sai_redis_notify_syncd(switch_id, attr);

            case SAI_REDIS_SWITCH_ATTR_USE_TEMP_VIEW:
                g_useTempView = attr->value.booldata;
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_RECORD_STATS:
                g_recordStats = attr->value.booldata;
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_SYNC_MODE:

                g_syncMode = attr->value.booldata;

                if (g_syncMode)
                {
                    SWSS_LOG_NOTICE("disabling buffered pipeline in sync mode");
                    g_asicState->setBuffered(false);
                }

                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_USE_PIPELINE:

                if (g_syncMode)
                {
                    SWSS_LOG_WARN("use pipeline is not supported in sync mode");
                    return SAI_STATUS_NOT_SUPPORTED;
                }

                g_asicState->setBuffered(attr->value.booldata);
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_FLUSH:
                g_asicState->flush();
                return SAI_STATUS_SUCCESS;

            case SAI_REDIS_SWITCH_ATTR_RECORDING_OUTPUT_DIR:
                if (g_recorder && g_recorder->setRecordingOutputDirectory(*attr))
                    return SAI_STATUS_SUCCESS;
                return SAI_STATUS_FAILURE;

            default:
                break;
        }
    }

    sai_status_t status = g_meta->set(
            SAI_OBJECT_TYPE_SWITCH,
            switch_id,
            attr,
            *g_remoteSaiInterface);

    return status;
}

REDIS_REMOVE(SWITCH,switch);
REDIS_GET(SWITCH,switch);

/**
 * @brief Switch method table retrieved with sai_api_query()
 */
REDIS_GENERIC_STATS(SWITCH, switch);

const sai_switch_api_t redis_switch_api = {

    REDIS_GENERIC_QUAD_API(switch)
    REDIS_GENERIC_STATS_API(switch)
};
