#include "VidManager.h"

#include "lib/inc/VirtualObjectIdManager.h"

#include "meta/sai_serialize.h"

#include "swss/logger.h"

using namespace syncd;

sai_object_id_t VidManager::switchIdQuery(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    if (objectId == SAI_NULL_OBJECT_ID)
    {
        return objectId;
    }

    auto swid = sairedis::VirtualObjectIdManager::switchIdQuery(objectId);

    if (swid == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_THROW("invalid object id %s",
                sai_serialize_object_id(objectId).c_str());
    }

    return swid;
}

sai_object_type_t VidManager::objectTypeQuery(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    if (objectId == SAI_NULL_OBJECT_ID)
    {
        return SAI_OBJECT_TYPE_NULL;
    }

    sai_object_type_t ot = sairedis::VirtualObjectIdManager::objectTypeQuery(objectId);

    if (ot == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_THROW("invalid object id %s",
                sai_serialize_object_id(objectId).c_str());
    }

    return ot;
}

uint32_t VidManager::getSwitchIndex(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    sai_object_id_t swid = VidManager::switchIdQuery(objectId);

    if (swid != SAI_NULL_OBJECT_ID)
    {
        return sairedis::VirtualObjectIdManager::getSwitchIndex(swid);
    }

    SWSS_LOG_THROW("invalid obejct id: %s, should be SWITCH",
            sai_serialize_object_id(objectId).c_str());

}

uint32_t VidManager::getGlobalContext(
        _In_ sai_object_id_t obejctId)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_ERROR("not implemented, FIXME");

    return 0; // TODO implement
}
