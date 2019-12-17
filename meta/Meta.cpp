#include "Meta.h"
#include "sai_meta.h"

#include "swss/logger.h"
#include "sai_serialize.h"

using namespace saimeta;

// TODO to be moved to private member methods
sai_status_t meta_generic_validation_remove(
        _In_ const sai_object_meta_key_t& meta_key);

sai_status_t meta_sai_validate_oid(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t* object_id,
        _In_ sai_object_id_t switch_id,
        _In_ bool create);

void meta_generic_validation_post_remove(
        _In_ const sai_object_meta_key_t& meta_key);

sai_status_t Meta::remove(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_oid(object_type, &object_id, SAI_NULL_OBJECT_ID, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = object_type, .objectkey = { .key = { .object_id  = object_id } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(object_type, object_id);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("remove status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("remove status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_remove(meta_key);
    }

    return status;
}

