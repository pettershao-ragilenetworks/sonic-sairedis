#include "sai_redis.h"

static sai_status_t redis_clear_port_all_stats(
        _In_ sai_object_id_t port_id)
{
    MUTEX();
    SWSS_LOG_ENTER();
    REDIS_CHECK_API_INITIALIZED();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

REDIS_GENERIC_QUAD(PORT,port);
REDIS_GENERIC_QUAD(PORT_POOL,port_pool);
REDIS_GENERIC_QUAD(PORT_SERDES,port_serdes);
REDIS_GENERIC_STATS(PORT,port);
REDIS_GENERIC_STATS(PORT_POOL,port_pool);

const sai_port_api_t redis_port_api = {

    REDIS_GENERIC_QUAD_API(port)
    REDIS_GENERIC_STATS_API(port)

    redis_clear_port_all_stats,

    REDIS_GENERIC_QUAD_API(port_pool)
    REDIS_GENERIC_STATS_API(port_pool)

    REDIS_GENERIC_QUAD_API(port_serdes)
};
