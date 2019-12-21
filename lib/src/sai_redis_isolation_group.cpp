#include "sai_redis.h"
#include "sai_redis_internal.h"

REDIS_GENERIC_QUAD(ISOLATION_GROUP,isolation_group);
REDIS_GENERIC_QUAD(ISOLATION_GROUP_MEMBER,isolation_group_member);

const sai_isolation_group_api_t redis_isolation_group_api = {

    REDIS_GENERIC_QUAD_API(isolation_group)
    REDIS_GENERIC_QUAD_API(isolation_group_member)
};

