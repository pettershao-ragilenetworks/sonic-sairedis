#include "Meta.h"
#include "sai_meta.h"

#include "swss/logger.h"
#include "sai_serialize.h"
#include "OidRefCounter.h"

#include <inttypes.h>

using namespace saimeta;

extern OidRefCounter g_oids;

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

sai_status_t meta_sai_validate_fdb_entry(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ bool create,
        _In_ bool get = false);

sai_status_t meta_sai_validate_mcast_fdb_entry(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ bool create,
        _In_ bool get = false);

sai_status_t meta_sai_validate_neighbor_entry(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ bool create);

sai_status_t meta_sai_validate_route_entry(
        _In_ const sai_route_entry_t* route_entry,
        _In_ bool create);

sai_status_t meta_sai_validate_l2mc_entry(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ bool create);

sai_status_t meta_sai_validate_ipmc_entry(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ bool create);

sai_status_t meta_sai_validate_nat_entry(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ bool create);

sai_status_t meta_sai_validate_inseg_entry(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ bool create);

sai_status_t meta_generic_validation_create(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_object_id_t switch_id,
        _In_ const uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list);

sai_status_t meta_generic_validation_set(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ const sai_attribute_t *attr);

sai_status_t meta_generic_validation_get(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ const uint32_t attr_count,
        _In_ sai_attribute_t *attr_list);

void meta_generic_validation_post_get(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_object_id_t switch_id,
        _In_ const uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list);

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

sai_status_t Meta::remove(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_fdb_entry(fdb_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_FDB_ENTRY, .objectkey = { .key = { .fdb_entry = *fdb_entry  } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(fdb_entry);

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

sai_status_t Meta::remove(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_mcast_fdb_entry(mcast_fdb_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_MCAST_FDB_ENTRY, .objectkey = { .key = { .mcast_fdb_entry = *mcast_fdb_entry  } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(mcast_fdb_entry);

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

sai_status_t Meta::remove(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_neighbor_entry(neighbor_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_NEIGHBOR_ENTRY, .objectkey = { .key = { .neighbor_entry = *neighbor_entry  } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(neighbor_entry);

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

sai_status_t Meta::remove(
        _In_ const sai_route_entry_t* route_entry,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_route_entry(route_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY, .objectkey = { .key = { .route_entry = *route_entry  } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(route_entry);

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

sai_status_t Meta::remove(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_l2mc_entry(l2mc_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_L2MC_ENTRY, .objectkey = { .key = { .l2mc_entry = *l2mc_entry  } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(l2mc_entry);

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

sai_status_t Meta::remove(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_ipmc_entry(ipmc_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_IPMC_ENTRY, .objectkey = { .key = { .ipmc_entry = *ipmc_entry  } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(ipmc_entry);

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

sai_status_t Meta::remove(
        _In_ const sai_nat_entry_t* nat_entry,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_nat_entry(nat_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_NAT_ENTRY, .objectkey = { .key = { .nat_entry = *nat_entry  } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(nat_entry);

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

sai_status_t Meta::remove(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_inseg_entry(inseg_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_INSEG_ENTRY, .objectkey = { .key = { .inseg_entry = *inseg_entry  } } };

    status = meta_generic_validation_remove(meta_key);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.remove(inseg_entry);

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

sai_status_t Meta::create(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_fdb_entry(fdb_entry, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_FDB_ENTRY, .objectkey = { .key = { .fdb_entry = *fdb_entry  } } };

    status = meta_generic_validation_create(meta_key, fdb_entry->switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(fdb_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_create(meta_key, fdb_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::create(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_mcast_fdb_entry(mcast_fdb_entry, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_MCAST_FDB_ENTRY, .objectkey = { .key = { .mcast_fdb_entry = *mcast_fdb_entry  } } };

    status = meta_generic_validation_create(meta_key, mcast_fdb_entry->switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(mcast_fdb_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_create(meta_key, mcast_fdb_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::create(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_neighbor_entry(neighbor_entry, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_NEIGHBOR_ENTRY, .objectkey = { .key = { .neighbor_entry = *neighbor_entry  } } };

    status = meta_generic_validation_create(meta_key, neighbor_entry->switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(neighbor_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_create(meta_key, neighbor_entry->switch_id, attr_count, attr_list);
    }

    return status;
}
sai_status_t Meta::create(
        _In_ const sai_route_entry_t* route_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_route_entry(route_entry, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY, .objectkey = { .key = { .route_entry = *route_entry  } } };

    status = meta_generic_validation_create(meta_key, route_entry->switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(route_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_create(meta_key, route_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::create(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_l2mc_entry(l2mc_entry, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_L2MC_ENTRY, .objectkey = { .key = { .l2mc_entry = *l2mc_entry  } } };

    status = meta_generic_validation_create(meta_key, l2mc_entry->switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(l2mc_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_create(meta_key, l2mc_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::create(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_ipmc_entry(ipmc_entry, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_IPMC_ENTRY, .objectkey = { .key = { .ipmc_entry = *ipmc_entry  } } };

    status = meta_generic_validation_create(meta_key, ipmc_entry->switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(ipmc_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_create(meta_key, ipmc_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::create(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_inseg_entry(inseg_entry, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_INSEG_ENTRY, .objectkey = { .key = { .inseg_entry = *inseg_entry  } } };

    status = meta_generic_validation_create(meta_key, inseg_entry->switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(inseg_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_create(meta_key, inseg_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::create(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_nat_entry(nat_entry, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_NAT_ENTRY, .objectkey = { .key = { .nat_entry = *nat_entry  } } };

    status = meta_generic_validation_create(meta_key, nat_entry->switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(nat_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_create(meta_key, nat_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_fdb_entry(fdb_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_FDB_ENTRY, .objectkey = { .key = { .fdb_entry = *fdb_entry  } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(fdb_entry, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_mcast_fdb_entry(mcast_fdb_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_MCAST_FDB_ENTRY, .objectkey = { .key = { .mcast_fdb_entry = *mcast_fdb_entry  } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(mcast_fdb_entry, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_neighbor_entry(neighbor_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_NEIGHBOR_ENTRY, .objectkey = { .key = { .neighbor_entry = *neighbor_entry  } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(neighbor_entry, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ const sai_route_entry_t* route_entry,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_route_entry(route_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY, .objectkey = { .key = { .route_entry = *route_entry  } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(route_entry, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_l2mc_entry(l2mc_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_L2MC_ENTRY, .objectkey = { .key = { .l2mc_entry = *l2mc_entry  } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(l2mc_entry, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_ipmc_entry(ipmc_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_IPMC_ENTRY, .objectkey = { .key = { .ipmc_entry = *ipmc_entry  } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(ipmc_entry, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_inseg_entry(inseg_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_INSEG_ENTRY, .objectkey = { .key = { .inseg_entry = *inseg_entry  } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(inseg_entry, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();
    sai_status_t status = meta_sai_validate_nat_entry(nat_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_NAT_ENTRY, .objectkey = { .key = { .nat_entry = *nat_entry  } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(nat_entry, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    // NOTE: when doing get, entry may not exist on metadata db

    sai_status_t status = meta_sai_validate_fdb_entry(fdb_entry, false, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_FDB_ENTRY, .objectkey = { .key = { .fdb_entry = *fdb_entry } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(fdb_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, fdb_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    // NOTE: when doing get, entry may not exist on metadata db

    sai_status_t status = meta_sai_validate_mcast_fdb_entry(mcast_fdb_entry, false, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_MCAST_FDB_ENTRY, .objectkey = { .key = { .mcast_fdb_entry = *mcast_fdb_entry } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(mcast_fdb_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, mcast_fdb_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_neighbor_entry(neighbor_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_NEIGHBOR_ENTRY, .objectkey = { .key = { .neighbor_entry = *neighbor_entry } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(neighbor_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, neighbor_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ const sai_route_entry_t* route_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_route_entry(route_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY, .objectkey = { .key = { .route_entry = *route_entry } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(route_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, route_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_l2mc_entry(l2mc_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_L2MC_ENTRY, .objectkey = { .key = { .l2mc_entry = *l2mc_entry } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(l2mc_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, l2mc_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_ipmc_entry(ipmc_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_IPMC_ENTRY, .objectkey = { .key = { .ipmc_entry = *ipmc_entry } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(ipmc_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, ipmc_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_inseg_entry(inseg_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_INSEG_ENTRY, .objectkey = { .key = { .inseg_entry = *inseg_entry } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(inseg_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, inseg_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_nat_entry(nat_entry, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_NAT_ENTRY, .objectkey = { .key = { .nat_entry = *nat_entry  } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(nat_entry, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, nat_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::create(
        _In_ sai_object_type_t object_type,
        _Out_ sai_object_id_t* object_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_oid(object_type, object_id, switch_id, true);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = object_type, .objectkey = { .key = { .object_id  = SAI_NULL_OBJECT_ID } } };

    status = meta_generic_validation_create(meta_key, switch_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.create(object_type, object_id, switch_id, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("create status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("create status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_key.objectkey.key.object_id = *object_id;

        if (meta_key.objecttype == SAI_OBJECT_TYPE_SWITCH)
        {
            /*
             * We are creating switch object, so switch id must be the same as
             * just created object. We could use SAI_NULL_OBJECT_ID in that
             * case and do special switch inside post_create method.
             */

            switch_id = *object_id;
        }

        meta_generic_validation_post_create(meta_key, switch_id, attr_count, attr_list);
    }

    return status;
}

sai_status_t Meta::set(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_attribute_t *attr,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_oid(object_type, &object_id, SAI_NULL_OBJECT_ID, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = object_type, .objectkey = { .key = { .object_id  = object_id } } };

    status = meta_generic_validation_set(meta_key, attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.set(object_type, object_id, attr);

    if (status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_DEBUG("set status: %s", sai_serialize_status(status).c_str());
    }
    else
    {
        SWSS_LOG_ERROR("set status: %s", sai_serialize_status(status).c_str());
    }

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_set(meta_key, attr);
    }

    return status;
}

sai_status_t Meta::get(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _Inout_ sairedis::SaiInterface& saiInterface)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_sai_validate_oid(object_type, &object_id, SAI_NULL_OBJECT_ID, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = object_type, .objectkey = { .key = { .object_id  = object_id } } };

    status = meta_generic_validation_get(meta_key, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    status = saiInterface.get(object_type, object_id, attr_count, attr_list);

    if (status == SAI_STATUS_SUCCESS)
    {
        sai_object_id_t switch_id = sai_switch_id_query(object_id);

        if (!g_oids.objectReferenceExists(switch_id))
        {
            SWSS_LOG_ERROR("switch id 0x%" PRIx64 " doesn't exist", switch_id);
        }

        meta_generic_validation_post_get(meta_key, switch_id, attr_count, attr_list);
    }

    return status;
}
