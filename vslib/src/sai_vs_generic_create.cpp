#include "sai_vs.h"
#include "sai_vs_state.h"
#include "sai_vs_switch_BCM56850.h"
#include "sai_vs_switch_MLNX2700.h"

SwitchStateMap g_switch_state_map;

void vs_update_real_object_ids(
        _In_ const std::shared_ptr<SwitchState> warmBootState)
{
    SWSS_LOG_ENTER();

    /*
     * Since we loaded state from warm boot, we need to update real object id's
     * in case a new object will be created. We need this so new objects will
     * not have the same ID as existing ones.
     */

    for (auto oh: warmBootState->objectHash)
    {
        sai_object_type_t ot = oh.first;

        if (ot == SAI_OBJECT_TYPE_NULL)
            continue;

        auto oi = sai_metadata_get_object_type_info(ot);

        if (oi == NULL)
        {
            SWSS_LOG_THROW("failed to find object type info for object type: %d", ot);
        }

        if (oi->isnonobjectid)
            continue;

        for (auto o: oh.second)
        {
            sai_object_id_t oid;

            sai_deserialize_object_id(o.first, oid);

            // TODO this should actually go to oid indexer, and be passed to
            // real object manager
            g_realObjectIdManager->updateWarmBootObjectIndex(oid);
        }
    }
}

std::shared_ptr<SwitchState> vs_read_switch_database_for_warm_restart(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    if (g_warm_boot_read_file == NULL)
    {
        SWSS_LOG_ERROR("warm boot read file is NULL");
        return nullptr;
    }

    std::ifstream dumpFile;

    dumpFile.open(g_warm_boot_read_file);

    if (!dumpFile.is_open())
    {
        SWSS_LOG_ERROR("failed to open: %s, switching to cold boot", g_warm_boot_read_file);

        g_vs_boot_type = SAI_VS_BOOT_TYPE_COLD;

        return nullptr;
    }

    std::shared_ptr<SwitchState> ss = std::make_shared<SwitchState>(switch_id);

    size_t count = 1; // count is 1 since switch_id was inserted to objectHash in SwitchState constructor

    std::string line;
    while (std::getline(dumpFile, line))
    {
        // line format: OBJECT_TYPE OBJECT_ID ATTR_ID ATTR_VALUE
        std::istringstream iss(line);

        std::string str_object_type;
        std::string str_object_id;
        std::string str_attr_id;
        std::string str_attr_value;

        iss >> str_object_type >> str_object_id;

        if (str_object_type == SAI_VS_FDB_INFO)
        {
            /*
             * If we read line from fdb info set and use tap device is enabled
             * just parse line and repopulate fdb info set.
             */

            if (g_vs_hostif_use_tap_device)
            {
                fdb_info_t fi;

                sai_vs_deserialize_fdb_info(str_object_id, fi);

                g_fdb_info_set.insert(fi);
            }

            continue;
        }

        iss >> str_attr_id >> str_attr_value;

        sai_object_meta_key_t meta_key;

        sai_deserialize_object_meta_key(str_object_type + ":" + str_object_id, meta_key);

        auto &objectHash = ss->objectHash.at(meta_key.objecttype);

        if (objectHash.find(str_object_id) == objectHash.end())
        {
            count++;

            objectHash[str_object_id] = {};
        }

        if (str_attr_id == "NULL")
        {
            // skip empty attributes
            continue;
        }

        if (meta_key.objecttype == SAI_OBJECT_TYPE_SWITCH)
        {
            if (meta_key.objectkey.key.object_id != switch_id)
            {
                SWSS_LOG_THROW("created switch id is %s but warm boot serialized is %s",
                        sai_serialize_object_id(switch_id).c_str(),
                        str_object_id.c_str());
            }
        }

        auto meta = sai_metadata_get_attr_metadata_by_attr_id_name(str_attr_id.c_str());

        if (meta == NULL)
        {
            SWSS_LOG_THROW("failed to find metadata for %s", str_attr_id.c_str());
        }

        // populate attributes

        sai_attribute_t attr;

        attr.id = meta->attrid;

        sai_deserialize_attr_value(str_attr_value.c_str(), *meta, attr, false);

        auto a = std::make_shared<SaiAttrWrap>(meta_key.objecttype, &attr);

        objectHash[str_object_id][a->getAttrMetadata()->attridname] = a;

        // free possible list attributes
        sai_deserialize_free_attribute_value(meta->attrvaluetype, attr);
    }

    // NOTE notification pointers should be restored by attr_list when creating switch

    dumpFile.close();

    if (g_vs_hostif_use_tap_device)
    {
        SWSS_LOG_NOTICE("loaded %zu fdb infos", g_fdb_info_set.size());
    }

    SWSS_LOG_NOTICE("loaded %zu objects from: %s", count, g_warm_boot_read_file);

    return ss;
}

void vs_validate_switch_warm_boot_atributes(
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    /*
     * When in warm boot, as init attributes on switch we only allow
     * notifications and init attribute.  Actually we should check if
     * notifications we pass are the same as the one that we have in dumped db,
     * if not we should set missing one to NULL ptr.
     */

    for (uint32_t i = 0; i < attr_count; ++i)
    {
        auto meta = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_SWITCH, attr_list[i].id);

        if (meta == NULL)
        {
            SWSS_LOG_THROW("failed to find metadata for switch attribute %d", attr_list[i].id);
        }

        if (meta->attrid == SAI_SWITCH_ATTR_INIT_SWITCH)
            continue;

        if (meta->attrvaluetype == SAI_ATTR_VALUE_TYPE_POINTER)
            continue;

        SWSS_LOG_THROW("attribute %s ist not INIT and not notification, not supported in warm boot", meta->attridname);
    }
}

void vs_update_local_metadata(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    /*
     * After warm boot we recreated all ASIC state, but since we are using
     * meta_* to check all needed data, we need to use post_create/post_set
     * methods to recreate state in local metadata so when next APIs will be
     * called, we could check the actual state.
     */

    auto &objectHash = g_switch_state_map.at(switch_id)->objectHash;//.at(object_type);

    // first create switch
    // first we need to create all "oid" objects to have reference base
    // then set all object attributes on those oids
    // then create all non oid like route etc.

    /*
     * First update switch, since all non switch objects will be using
     * sai_switch_id_query to check if oid is valid.
     */

    sai_object_meta_key_t mk;

    mk.objecttype = SAI_OBJECT_TYPE_SWITCH;
    mk.objectkey.key.object_id = switch_id;

    meta_generic_validation_post_create(mk, switch_id, 0, NULL);

    /*
     * Create every non object id except switch. Switch object was already
     * created above, and non object ids like route may contain other object
     * id's inside *_entry struct, and since metadata is checking reference of
     * those objects, they must exists first.
     */

    for (auto kvp: objectHash)
    {
        sai_object_type_t ot = kvp.first;

        if (ot == SAI_OBJECT_TYPE_NULL)
            continue;

        if (ot == SAI_OBJECT_TYPE_SWITCH)
            continue;

        auto info = sai_metadata_get_object_type_info(ot);

        if (info == NULL)
            SWSS_LOG_THROW("failed to get object type info for object type %d", ot);

        if (info->isnonobjectid)
            continue;

        mk.objecttype = ot;

        for (auto obj: kvp.second)
        {
            sai_deserialize_object_id(obj.first, mk.objectkey.key.object_id);

            meta_generic_validation_post_create(mk, switch_id, 0, NULL);
        }
    }

    /*
     * Create all non object id's. All oids are created, so objects inside
     * *_entry structs can be referenced correctly.
     */

    for (auto kvp: objectHash)
    {
        sai_object_type_t ot = kvp.first;

        if (ot == SAI_OBJECT_TYPE_NULL)
            continue;

        auto info = sai_metadata_get_object_type_info(ot);

        if (info == NULL)
            SWSS_LOG_THROW("failed to get object type info for object type %d", ot);

        if (info->isobjectid)
            continue;

        for (auto obj: kvp.second)
        {
            std::string key = std::string(info->objecttypename) + ":" + obj.first;

            sai_deserialize_object_meta_key(key, mk);

            meta_generic_validation_post_create(mk, switch_id, 0, NULL);
        }
    }

    /*
     * Set all attributes on all objects. Since attributes maybe OID attributes
     * we need to set them too for correct reference count.
     */

    for (auto kvp: objectHash)
    {
        sai_object_type_t ot = kvp.first;

        if (ot == SAI_OBJECT_TYPE_NULL)
            continue;

        auto info = sai_metadata_get_object_type_info(ot);

        if (info == NULL)
            SWSS_LOG_THROW("failed to get object type info for object type %d", ot);

        for (auto obj: kvp.second)
        {
            std::string key = std::string(info->objecttypename) + ":" + obj.first;

            sai_deserialize_object_meta_key(key, mk);

            for (auto a: obj.second)
            {
                auto meta = a.second->getAttrMetadata();

                if (meta->isreadonly)
                    continue;

                meta_generic_validation_post_set(mk, a.second->getAttr());
            }
        }
    }

    /*
     * Since this method is called inside internal_vs_generic_create next
     * meta_generic_validation_post_create will be called after success return
     * of meta_sai_create_oid and it would fail since we already created switch
     * so we need to notify metadata that this is warm boot.
     */

    meta_warm_boot_notify();
}

sai_status_t internal_vs_generic_create(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &serialized_object_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    if (object_type == SAI_OBJECT_TYPE_SWITCH)
    {
        std::shared_ptr<SwitchState> warmBootState = nullptr;

        if (g_vs_boot_type == SAI_VS_BOOT_TYPE_WARM)
        {
            warmBootState = vs_read_switch_database_for_warm_restart(switch_id);

            vs_validate_switch_warm_boot_atributes(attr_count, attr_list);
        }

        switch (g_vs_switch_type)
        {
            case SAI_VS_SWITCH_TYPE_BCM56850:
                init_switch_BCM56850(switch_id, warmBootState);
                break;

            case SAI_VS_SWITCH_TYPE_MLNX2700:
                init_switch_MLNX2700(switch_id, warmBootState);
                break;

            default:
                SWSS_LOG_WARN("unknown switch type: %d", g_vs_switch_type);
                return SAI_STATUS_FAILURE;
        }

        if (warmBootState != nullptr)
        {
            vs_update_real_object_ids(warmBootState);

            vs_update_local_metadata(switch_id);

            if (g_vs_hostif_use_tap_device)
            {
                vs_recreate_hostif_tap_interfaces(switch_id);
            }
        }
    }

    auto &objectHash = g_switch_state_map.at(switch_id)->objectHash.at(object_type);

    auto it = objectHash.find(serialized_object_id);

    if (object_type != SAI_OBJECT_TYPE_SWITCH)
    {
        /*
         * Switch is special, and object is already created by init.
         *
         * XXX revisit this.
         */

        if (it != objectHash.end())
        {
            SWSS_LOG_ERROR("create failed, object already exists, object type: %s: id: %s",
                    sai_serialize_object_type(object_type).c_str(),
                    serialized_object_id.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }
    }

    if (objectHash.find(serialized_object_id) == objectHash.end())
    {
        /*
         * Number of attributes may be zero, so see if actual entry was created
         * with empty hash.
         */

        objectHash[serialized_object_id] = {};
    }

    for (uint32_t i = 0; i < attr_count; ++i)
    {
        auto a = std::make_shared<SaiAttrWrap>(object_type, &attr_list[i]);

        objectHash[serialized_object_id][a->getAttrMetadata()->attridname] = a;
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t vs_generic_create(
        _In_ sai_object_type_t object_type,
        _Out_ sai_object_id_t *object_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    // create new real object ID
    *object_id = g_realObjectIdManager->allocateNewObjectId(object_type, switch_id);

    if (object_type == SAI_OBJECT_TYPE_SWITCH)
    {
        switch_id = *object_id;
    }

    std::string str_object_id = sai_serialize_object_id(*object_id);

    return internal_vs_generic_create(
            object_type,
            str_object_id,
            switch_id,
            attr_count,
            attr_list);
}

#define VS_ENTRY_CREATE(OT,ot)                          \
    sai_status_t vs_generic_create_ ## ot(              \
            _In_ const sai_ ## ot ## _t * entry,        \
            _In_ uint32_t attr_count,                   \
            _In_ const sai_attribute_t *attr_list)      \
    {                                                   \
        SWSS_LOG_ENTER();                               \
        std::string str = sai_serialize_ ## ot(*entry); \
        return internal_vs_generic_create(              \
                SAI_OBJECT_TYPE_ ## OT,                 \
                str,                                    \
                entry->switch_id,                       \
                attr_count,                             \
                attr_list);                             \
    }

VS_ENTRY_CREATE(FDB_ENTRY,fdb_entry);
VS_ENTRY_CREATE(INSEG_ENTRY,inseg_entry);
VS_ENTRY_CREATE(IPMC_ENTRY,ipmc_entry);
VS_ENTRY_CREATE(L2MC_ENTRY,l2mc_entry);
VS_ENTRY_CREATE(MCAST_FDB_ENTRY,mcast_fdb_entry);
VS_ENTRY_CREATE(NEIGHBOR_ENTRY,neighbor_entry);
VS_ENTRY_CREATE(ROUTE_ENTRY,route_entry);
VS_ENTRY_CREATE(NAT_ENTRY, nat_entry);
