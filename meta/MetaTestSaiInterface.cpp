#include "MetaTestSaiInterface.h"
#include "NumberOidIndexGenerator.h"

#include "swss/logger.h"
#include "sai_serialize.h"

using namespace saimeta;

MetaTestSaiInterface::MetaTestSaiInterface()
{
    SWSS_LOG_ENTER();

    m_virtualObjectIdManager = 
        std::make_shared<sairedis::VirtualObjectIdManager>(0,
                std::make_shared<NumberOidIndexGenerator>());
}

sai_status_t MetaTestSaiInterface::create(
        _In_ sai_object_type_t objectType,
        _Out_ sai_object_id_t* objectId,
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    *objectId = m_virtualObjectIdManager->allocateNewObjectId(objectType, switchId);

    if (*objectId == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("failed to allocated new object id: %s:%s",
                sai_serialize_object_type(objectType).c_str(),
                sai_serialize_object_id(switchId).c_str());

        return SAI_STATUS_FAILURE;
    }

    return SAI_STATUS_SUCCESS;
}

sai_object_type_t MetaTestSaiInterface::objectTypeQuery(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return m_virtualObjectIdManager->saiObjectTypeQuery(objectId);
}

sai_object_id_t MetaTestSaiInterface::switchIdQuery(
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    return m_virtualObjectIdManager->saiSwitchIdQuery(objectId);
}

