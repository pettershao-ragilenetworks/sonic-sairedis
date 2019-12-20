#include "SaiAttrWrapper.h"
#include "OidRefCounter.h"
#include "AttrKeyMap.h"
#include "Globals.h"
#include "SaiObjectCollection.h"
#include "PortRelatedSet.h"

#include "sai_meta.h"
#include "sai_serialize.h"

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <iostream>
#include <vector>
#include <unordered_map>
#include <set>
#include <memory>
#include <map>
#include <iterator>

#define META_LOG_STATUS(op, s)\
    if (s == SAI_STATUS_SUCCESS)                                           \
        SWSS_LOG_DEBUG(#op " status: %s", sai_serialize_status(s).c_str()); \
    else if (s == SAI_STATUS_BUFFER_OVERFLOW                               \
               || SAI_STATUS_IS_ATTR_NOT_IMPLEMENTED(s)                    \
               || SAI_STATUS_IS_ATTR_NOT_SUPPORTED(s))                     \
        SWSS_LOG_INFO(#op " status: %s", sai_serialize_status(s).c_str());  \
    else                                                                   \
        SWSS_LOG_ERROR(#op " status: %s", sai_serialize_status(s).c_str());

using namespace saimeta;

static volatile bool unittests_enabled = false;

static std::set<std::string> meta_unittests_set_readonly_set;

void meta_unittests_enable(
        _In_ bool enable)
{
    SWSS_LOG_ENTER();

    unittests_enabled = enable;
}

bool meta_unittests_enabled()
{
    SWSS_LOG_ENTER();

    return unittests_enabled;
}

sai_status_t meta_unittests_allow_readonly_set_once(
        _In_ sai_object_type_t object_type,
        _In_ int32_t attr_id)
{
    SWSS_LOG_ENTER();

    if (!unittests_enabled)
    {
        SWSS_LOG_NOTICE("unittests are not enabled");
        return SAI_STATUS_FAILURE;
    }

    auto *md = sai_metadata_get_attr_metadata(object_type, attr_id);

    if (md == NULL)
    {
        SWSS_LOG_ERROR("failed to get metadata for object type %d and attr id %d", object_type, attr_id);
        return SAI_STATUS_FAILURE;
    }

    if (!SAI_HAS_FLAG_READ_ONLY(md->flags))
    {
        SWSS_LOG_ERROR("attribute %s is not marked as READ_ONLY", md->attridname);
        return SAI_STATUS_FAILURE;
    }

    meta_unittests_set_readonly_set.insert(md->attridname);

    SWSS_LOG_INFO("enabling SET for readonly attribute: %s", md->attridname);

    return SAI_STATUS_SUCCESS;
}

static bool meta_unittests_get_and_erase_set_readonly_flag(
        _In_ const sai_attr_metadata_t& md)
{
    SWSS_LOG_ENTER();

    if (!unittests_enabled)
    {
        // explicitly to not produce false alarms
        SWSS_LOG_NOTICE("unittests are not enabled");
        return false;
    }

    const auto &it = meta_unittests_set_readonly_set.find(md.attridname);

    if (it == meta_unittests_set_readonly_set.end())
    {
        SWSS_LOG_ERROR("%s is not present in readonly set", md.attridname);
        return false;
    }

    SWSS_LOG_INFO("%s is present in readonly set, erasing", md.attridname);

    meta_unittests_set_readonly_set.erase(it);

    return true;
}


// TODO move to metadata utils
bool is_ipv6_mask_valid(
        _In_ const uint8_t* mask)
{
    SWSS_LOG_ENTER();

    if (mask == NULL)
    {
        SWSS_LOG_ERROR("mask is null");

        return false;
    }

    int ones = 0;
    bool zeros = false;

    for (uint8_t i = 0; i < 128; i++)
    {
        bool bit = mask[i/8] & (1 << (7 - (i%8)));

        if (zeros && bit)
        {
            return false;
        }

        zeros |= !bit;

        if (bit)
        {
            ones++;
        }
    }

    return true;
}

std::vector<const sai_attr_metadata_t*> get_attributes_metadata(
        _In_ sai_object_type_t objecttype)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_DEBUG("objecttype: %s", sai_serialize_object_type(objecttype).c_str());

    auto meta = sai_metadata_get_object_type_info(objecttype)->attrmetadata;

    std::vector<const sai_attr_metadata_t*> attrs;

    for (size_t index = 0; meta[index] != NULL; ++index)
    {
        attrs.push_back(meta[index]);
    }

    return attrs;
}

// TODO should we have this set per switch ?
// to keep each switch in separate object?

/**
 * @brief Port related objects set.
 *
 * Key in map is port OID, and value is set of related objects ids
 * like queues, ipgs and scheduler groups.
 *
 * This map will help to identify objects to be automatically removed
 * when port will be removed.
 */
PortRelatedSet g_portRelatedSet;

/*
 * Non object ids don't need reference count since they are leafs and can be
 * removed at any time.
 */

OidRefCounter g_oids;
AttrKeyMap g_attrKeys;
SaiObjectCollection g_saiObjectCollection;

// TODO check on create switch if we have any oid objects passed -
// is this necessary, we won't have any oids yet anyway
//
// TODO check if all oids passed belong to single switch

// TODO to be removed
void dump_object_reference()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("dump references in meta");

    for (auto kvp: g_oids.getAllReferences())
    {
        sai_object_id_t oid = kvp.first;

        sai_object_type_t ot = sai_object_type_query(oid);

        switch (ot)
        {

            case SAI_OBJECT_TYPE_LAG:
            case SAI_OBJECT_TYPE_NEXT_HOP:
            case SAI_OBJECT_TYPE_NEXT_HOP_GROUP:
            case SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER:
            case SAI_OBJECT_TYPE_ROUTE_ENTRY:
            case SAI_OBJECT_TYPE_ROUTER_INTERFACE:
                break;

                // skip object we have no interest in
            default:
                continue;
        }

        SWSS_LOG_NOTICE("ref %s: %s: %d",
                sai_serialize_object_type(ot).c_str(),
                sai_serialize_object_id(oid).c_str(),
                kvp.second);
    }
}

// GENERIC REFERENCE FUNCTIONS

// make sure db is initialized before usage
static sai_status_t inited = meta_init_db();

sai_status_t meta_init_db()
{
    SWSS_LOG_ENTER();

    /*
     * This DB will contain objects from all switches.
     */

    g_oids.clear();
    g_saiObjectCollection.clear();
    g_attrKeys.clear();
    g_portRelatedSet.clear();

    return SAI_STATUS_SUCCESS;
}

std::shared_ptr<SaiAttrWrapper> get_object_previous_attr(
        _In_ const sai_object_meta_key_t& metaKey,
        _In_ const sai_attr_metadata_t& md)
{
    SWSS_LOG_ENTER();

    return g_saiObjectCollection.getObjectAttr(metaKey, md.attrid);
}

sai_status_t meta_generic_validation_objlist(
        _In_ const sai_attr_metadata_t& md,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t count,
        _In_ const sai_object_id_t* list)
{
    SWSS_LOG_ENTER();

    if (count > MAX_LIST_COUNT)
    {
        META_LOG_ERROR(md, "object list count %u > max list count %u", count, MAX_LIST_COUNT);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (list == NULL)
    {
        if (count == 0)
        {
            return SAI_STATUS_SUCCESS;
        }

        META_LOG_ERROR(md, "object list is null, but count is %u", count);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    /*
     * We need oids set and object type to check whether oids are not repeated
     * on list and whether all oids are same object type.
     */

    std::set<sai_object_id_t> oids;

    sai_object_type_t object_type = SAI_OBJECT_TYPE_NULL;

    for (uint32_t i = 0; i < count; ++i)
    {
        sai_object_id_t oid = list[i];

        if (oids.find(oid) != oids.end())
        {
            META_LOG_ERROR(md, "object on list [%u] oid 0x%" PRIx64 " is duplicated, but not allowed", i, oid);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (oid == SAI_NULL_OBJECT_ID)
        {
            if (md.allownullobjectid)
            {
                // ok, null object is allowed
                continue;
            }

            META_LOG_ERROR(md, "object on list [%u] is NULL, but not allowed", i);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        oids.insert(oid);

        sai_object_type_t ot = sai_object_type_query(oid);

        if (ot == SAI_NULL_OBJECT_ID)
        {
            META_LOG_ERROR(md, "object on list [%u] oid 0x%" PRIx64 " is not valid, returned null object id", i, oid);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (!sai_metadata_is_allowed_object_type(&md, ot))
        {
            META_LOG_ERROR(md, "object on list [%u] oid 0x%" PRIx64 " object type %d is not allowed on this attribute", i, oid, ot);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (!g_oids.objectReferenceExists(oid))
        {
            META_LOG_ERROR(md, "object on list [%u] oid 0x%" PRIx64 " object type %d does not exists in local DB", i, oid, ot);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (i > 1)
        {
            /*
             * Currently all objects on list must be the same type.
             */

            if (object_type != ot)
            {
                META_LOG_ERROR(md, "object list contain's mixed object types: %d vs %d, not allowed", object_type, ot);

                return SAI_STATUS_INVALID_PARAMETER;
            }
        }

        sai_object_id_t query_switch_id = sai_switch_id_query(oid);

        if (!g_oids.objectReferenceExists(query_switch_id))
        {
            SWSS_LOG_ERROR("switch id 0x%" PRIx64 " doesn't exist", query_switch_id);
            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (query_switch_id != switch_id)
        {
            SWSS_LOG_ERROR("oid 0x%" PRIx64 " is from switch 0x%" PRIx64 " but expected switch 0x%" PRIx64 "", oid, query_switch_id, switch_id);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        object_type = ot;
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_genetic_validation_list(
        _In_ const sai_attr_metadata_t& md,
        _In_ uint32_t count,
        _In_ const void* list)
{
    SWSS_LOG_ENTER();

    if (count > MAX_LIST_COUNT)
    {
        META_LOG_ERROR(md, "list count %u > max list count %u", count, MAX_LIST_COUNT);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (count == 0 && list != NULL)
    {
        META_LOG_ERROR(md, "when count is zero, list must be NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (list == NULL)
    {
        if (count == 0)
        {
            return SAI_STATUS_SUCCESS;
        }

        META_LOG_ERROR(md, "list is null, but count is %u", count);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    return SAI_STATUS_SUCCESS;
}

#define VALIDATION_LIST(md,vlist) \
{\
    auto status1 = meta_genetic_validation_list(md,vlist.count,vlist.list);\
    if (status1 != SAI_STATUS_SUCCESS)\
    {\
        return status1;\
    }\
}

sai_object_id_t meta_extract_switch_id(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    /*
     * We assume here that objecttype in meta key is in valid range.
     */

    auto info = sai_metadata_get_object_type_info(meta_key.objecttype);

    if (info->isnonobjectid)
    {
        /*
         * Since object is non object id, we are sure via sanity check that
         * struct member contains switch_id, we need to extract it here.
         *
         * NOTE: we could have this in metadata predefined for all non object ids.
         */

        for (size_t j = 0; j < info->structmemberscount; ++j)
        {
            const sai_struct_member_info_t *m = info->structmembers[j];

            if (m->membervaluetype != SAI_ATTR_VALUE_TYPE_OBJECT_ID)
            {
                continue;
            }

            for (size_t k = 0 ; k < m->allowedobjecttypeslength; k++)
            {
                sai_object_type_t ot = m->allowedobjecttypes[k];

                if (ot == SAI_OBJECT_TYPE_SWITCH)
                {
                    return  m->getoid(&meta_key);
                }
            }
        }

        SWSS_LOG_ERROR("unable to find switch id inside non object id");

        return SAI_NULL_OBJECT_ID;
    }
    else
    {
        // NOTE: maybe we should extract switch from oid?
        return switch_id;
    }
}

sai_status_t meta_generic_validate_non_object_on_create(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    /*
     * Since non object id objects can contain several object id's inside
     * object id structure, we need to check whether they all belong to the
     * same switch (sine multiple switches can be present and whether all those
     * objects are allowed respectively on their members.
     *
     * This check is required only on creation, since on set/get/remove we
     * check in object hash whether this object exists.
     */

    auto info = sai_metadata_get_object_type_info(meta_key.objecttype);

    if (!info->isnonobjectid)
    {
        return SAI_STATUS_SUCCESS;
    }

    /*
     * This will be most utilized for creating route entries.
     */

    for (size_t j = 0; j < info->structmemberscount; ++j)
    {
        const sai_struct_member_info_t *m = info->structmembers[j];

        if (m->membervaluetype != SAI_ATTR_VALUE_TYPE_OBJECT_ID)
        {
            continue;
        }

        sai_object_id_t oid = m->getoid(&meta_key);

        if (oid == SAI_NULL_OBJECT_ID)
        {
            if (meta_key.objecttype == SAI_OBJECT_TYPE_FDB_ENTRY)
            {
                SWSS_LOG_WARN("workaround: %s is NULL, REMOVE when using bv_id", m->membername);
                continue;
            }

            SWSS_LOG_ERROR("oid on %s on struct member %s is NULL",
                    sai_serialize_object_type(meta_key.objecttype).c_str(),
                    m->membername);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (oid == SAI_NULL_OBJECT_ID)
        {
            SWSS_LOG_ERROR("oid on %s on struct member %s is NULL",
                    sai_serialize_object_type(meta_key.objecttype).c_str(),
                    m->membername);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (!g_oids.objectReferenceExists(oid))
        {
            SWSS_LOG_ERROR("object don't exist 0x%" PRIx64 " (%s)", oid, m->membername);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        sai_object_type_t ot = sai_object_type_query(oid);

        /*
         * No need for checking null here, since metadata don't allow
         * NULL in allowed objects list.
         */

        bool allowed = false;

        for (size_t k = 0 ; k < m->allowedobjecttypeslength; k++)
        {
            if (ot == m->allowedobjecttypes[k])
            {
                allowed = true;
                break;
            }
        }

        if (!allowed)
        {
            SWSS_LOG_ERROR("object id 0x%" PRIx64 " is %s, but it's not allowed on member %s",
                    oid, sai_serialize_object_type(ot).c_str(), m->membername);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        sai_object_id_t oid_switch_id = sai_switch_id_query(oid);

        if (!g_oids.objectReferenceExists(oid_switch_id))
        {
            SWSS_LOG_ERROR("switch id 0x%" PRIx64 " doesn't exist", oid_switch_id);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (switch_id != oid_switch_id)
        {
            SWSS_LOG_ERROR("oid 0x%" PRIx64 " is on switch 0x%" PRIx64 " but required switch is 0x%" PRIx64 "", oid, oid_switch_id, switch_id);

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_generic_validation_create(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_object_id_t switch_id,
        _In_ const uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    if (attr_count > MAX_LIST_COUNT)
    {
        SWSS_LOG_ERROR("create attribute count %u > max list count %u", attr_count, MAX_LIST_COUNT);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (attr_count > 0 && attr_list == NULL)
    {
        SWSS_LOG_ERROR("attr count is %u but attribute list pointer is NULL", attr_count);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    bool switchcreate = meta_key.objecttype == SAI_OBJECT_TYPE_SWITCH;

    if (switchcreate)
    {
        // we are creating switch

        switch_id = SAI_NULL_OBJECT_ID;
    }
    else
    {
        /*
         * Non switch object case (also non object id)
         *
         * NOTE: this is a lot of checks for each create
         */

        switch_id = meta_extract_switch_id(meta_key, switch_id);

        if (switch_id == SAI_NULL_OBJECT_ID)
        {
            SWSS_LOG_ERROR("switch id is NULL for %s", sai_serialize_object_type(meta_key.objecttype).c_str());

            return SAI_STATUS_INVALID_PARAMETER;
        }

        sai_object_type_t sw_type = sai_object_type_query(switch_id);

        if (sw_type != SAI_OBJECT_TYPE_SWITCH)
        {
            SWSS_LOG_ERROR("switch id 0x%" PRIx64 " type is %s, expected SWITCH", switch_id, sai_serialize_object_type(sw_type).c_str());

            return SAI_STATUS_INVALID_PARAMETER;
        }

        // check if switch exists

        sai_object_meta_key_t switch_meta_key = { .objecttype = SAI_OBJECT_TYPE_SWITCH, .objectkey = { .key = { .object_id = switch_id } } };

        if (!g_saiObjectCollection.objectExists(switch_meta_key))
        {
            SWSS_LOG_ERROR("switch id 0x%" PRIx64 " doesn't exist yet", switch_id);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (!g_oids.objectReferenceExists(switch_id))
        {
            SWSS_LOG_ERROR("switch id 0x%" PRIx64 " doesn't exist yet", switch_id);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        // ok
    }

    sai_status_t status = meta_generic_validate_non_object_on_create(meta_key, switch_id);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    std::unordered_map<sai_attr_id_t, const sai_attribute_t*> attrs;

    SWSS_LOG_DEBUG("attr count = %u", attr_count);

    bool haskeys = false;

    // check each attribute separately
    for (uint32_t idx = 0; idx < attr_count; ++idx)
    {
        const sai_attribute_t* attr = &attr_list[idx];

        auto mdp = sai_metadata_get_attr_metadata(meta_key.objecttype, attr->id);

        if (mdp == NULL)
        {
            SWSS_LOG_ERROR("unable to find attribute metadata %d:%d", meta_key.objecttype, attr->id);

            return SAI_STATUS_FAILURE;
        }

        const sai_attribute_value_t& value = attr->value;

        const sai_attr_metadata_t& md = *mdp;

        META_LOG_DEBUG(md, "(create)");

        if (attrs.find(attr->id) != attrs.end())
        {
            META_LOG_ERROR(md, "attribute id (%u) is defined on attr list multiple times", attr->id);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        attrs[attr->id] = attr;

        if (SAI_HAS_FLAG_READ_ONLY(md.flags))
        {
            META_LOG_ERROR(md, "attr is read only and cannot be created");

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (SAI_HAS_FLAG_KEY(md.flags))
        {
            haskeys = true;

            META_LOG_DEBUG(md, "attr is key");
        }

        // if we set OID check if exists and if type is correct
        // and it belongs to the same switch id

        switch (md.attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_CHARDATA:

                {
                    const char* chardata = value.chardata;

                    size_t len = strnlen(chardata, SAI_HOSTIF_NAME_SIZE);

                    if (len == SAI_HOSTIF_NAME_SIZE)
                    {
                        META_LOG_ERROR(md, "host interface name is too long");

                        return SAI_STATUS_INVALID_PARAMETER;
                    }

                    if (len == 0)
                    {
                        META_LOG_ERROR(md, "host interface name is zero");

                        return SAI_STATUS_INVALID_PARAMETER;
                    }

                    for (size_t i = 0; i < len; ++i)
                    {
                        char c = chardata[i];

                        if (c < 0x20 || c > 0x7e)
                        {
                            META_LOG_ERROR(md, "interface name contains invalid character 0x%02x", c);

                            return SAI_STATUS_INVALID_PARAMETER;
                        }
                    }

                    // TODO check whether name is not used by other host interface
                    break;
                }

            case SAI_ATTR_VALUE_TYPE_BOOL:
            case SAI_ATTR_VALUE_TYPE_UINT8:
            case SAI_ATTR_VALUE_TYPE_INT8:
            case SAI_ATTR_VALUE_TYPE_UINT16:
            case SAI_ATTR_VALUE_TYPE_INT16:
            case SAI_ATTR_VALUE_TYPE_UINT32:
            case SAI_ATTR_VALUE_TYPE_INT32:
            case SAI_ATTR_VALUE_TYPE_UINT64:
            case SAI_ATTR_VALUE_TYPE_INT64:
            case SAI_ATTR_VALUE_TYPE_MAC:
            case SAI_ATTR_VALUE_TYPE_IPV4:
            case SAI_ATTR_VALUE_TYPE_IPV6:
            case SAI_ATTR_VALUE_TYPE_POINTER:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS:

                {
                    switch (value.ipaddr.addr_family)
                    {
                        case SAI_IP_ADDR_FAMILY_IPV4:
                        case SAI_IP_ADDR_FAMILY_IPV6:
                            break;

                        default:

                            SWSS_LOG_ERROR("invalid address family: %d", value.ipaddr.addr_family);

                            return SAI_STATUS_INVALID_PARAMETER;
                    }

                    break;
                }

            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:

                {
                    status = meta_generic_validation_objlist(md, switch_id, 1, &value.oid);

                    if (status != SAI_STATUS_SUCCESS)
                    {
                        return status;
                    }

                    break;
                }

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:

                {
                    status = meta_generic_validation_objlist(md, switch_id, value.objlist.count, value.objlist.list);

                    if (status != SAI_STATUS_SUCCESS)
                    {
                        return status;
                    }

                    break;
                }

                // ACL FIELD

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV6:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:

                {
                    if (!value.aclfield.enable)
                    {
                        break;
                    }

                    status = meta_generic_validation_objlist(md, switch_id, 1, &value.aclfield.data.oid);

                    if (status != SAI_STATUS_SUCCESS)
                    {
                        return status;
                    }

                    break;
                }

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:

                {
                    if (!value.aclfield.enable)
                    {
                        break;
                    }

                    status = meta_generic_validation_objlist(md, switch_id, value.aclfield.data.objlist.count, value.aclfield.data.objlist.list);

                    if (status != SAI_STATUS_SUCCESS)
                    {
                        return status;
                    }

                    break;
                }

                // case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8_LIST:

                // ACL ACTION

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV6:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:

                {
                    if (!value.aclaction.enable)
                    {
                        break;
                    }

                    status = meta_generic_validation_objlist(md, switch_id, 1, &value.aclaction.parameter.oid);

                    if (status != SAI_STATUS_SUCCESS)
                    {
                        return status;
                    }

                    break;
                }

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:

                {
                    if (!value.aclaction.enable)
                    {
                        break;
                    }

                    status = meta_generic_validation_objlist(md, switch_id, value.aclaction.parameter.objlist.count, value.aclaction.parameter.objlist.list);

                    if (status != SAI_STATUS_SUCCESS)
                    {
                        return status;
                    }

                    break;
                }

                // ACL END

            case SAI_ATTR_VALUE_TYPE_UINT8_LIST:
                VALIDATION_LIST(md, value.u8list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT8_LIST:
                VALIDATION_LIST(md, value.s8list);
                break;
            case SAI_ATTR_VALUE_TYPE_UINT16_LIST:
                VALIDATION_LIST(md, value.u16list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT16_LIST:
                VALIDATION_LIST(md, value.s16list);
                break;
            case SAI_ATTR_VALUE_TYPE_UINT32_LIST:
                VALIDATION_LIST(md, value.u32list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT32_LIST:
                VALIDATION_LIST(md, value.s32list);
                break;
            case SAI_ATTR_VALUE_TYPE_QOS_MAP_LIST:
                VALIDATION_LIST(md, value.qosmap);
                break;
            case SAI_ATTR_VALUE_TYPE_ACL_RESOURCE_LIST:
                VALIDATION_LIST(md, value.aclresource);
                break;
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS_LIST:
                VALIDATION_LIST(md, value.ipaddrlist);
                break;

            case SAI_ATTR_VALUE_TYPE_UINT32_RANGE:

                if (value.u32range.min > value.u32range.max)
                {
                    META_LOG_ERROR(md, "invalid range %u .. %u", value.u32range.min, value.u32range.max);

                    return SAI_STATUS_INVALID_PARAMETER;
                }

                break;

            case SAI_ATTR_VALUE_TYPE_INT32_RANGE:

                if (value.s32range.min > value.s32range.max)
                {
                    META_LOG_ERROR(md, "invalid range %u .. %u", value.s32range.min, value.s32range.max);

                    return SAI_STATUS_INVALID_PARAMETER;
                }

                break;

            case SAI_ATTR_VALUE_TYPE_IP_PREFIX:

                {
                    switch (value.ipprefix.addr_family)
                    {
                        case SAI_IP_ADDR_FAMILY_IPV4:
                        case SAI_IP_ADDR_FAMILY_IPV6:
                            break;

                        default:

                            SWSS_LOG_ERROR("invalid address family: %d", value.ipprefix.addr_family);

                            return SAI_STATUS_INVALID_PARAMETER;
                    }

                    break;
                }

            default:

                META_LOG_THROW(md, "serialization type is not supported yet FIXME");
        }

        if (md.isenum)
        {
            int32_t val = value.s32;

            switch (md.attrvaluetype)
            {
                case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
                    val = value.aclfield.data.s32;
                    break;

                case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
                    val = value.aclaction.parameter.s32;
                    break;

                default:
                    val = value.s32;
                    break;
            }

            if (!sai_metadata_is_allowed_enum_value(&md, val))
            {
                META_LOG_ERROR(md, "is enum, but value %d not found on allowed values list", val);

                return SAI_STATUS_INVALID_PARAMETER;
            }
        }

        if (md.isenumlist)
        {
            // we allow repeats on enum list
            if (value.s32list.count != 0 && value.s32list.list == NULL)
            {
                META_LOG_ERROR(md, "enum list is NULL");

                return SAI_STATUS_INVALID_PARAMETER;
            }

            for (uint32_t i = value.s32list.count; i < value.s32list.count; ++i)
            {
                int32_t s32 = value.s32list.list[i];

                if (!sai_metadata_is_allowed_enum_value(&md, s32))
                {
                    META_LOG_ERROR(md, "is enum list, but value %d not found on allowed values list", s32);

                    return SAI_STATUS_INVALID_PARAMETER;
                }
            }
        }

        // conditions are checked later on
    }

    // we are creating object, no need for check if exists (only key values needs to be checked)

    auto info = sai_metadata_get_object_type_info(meta_key.objecttype);

    if (info->isnonobjectid)
    {
        // just sanity check if object already exists
        std::string key = sai_serialize_object_meta_key(meta_key);

        if (g_saiObjectCollection.objectExists(key))
        {
            SWSS_LOG_ERROR("object key %s already exists", key.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }
    }
    else
    {
        /*
         * We are creating OID object, and we don't have it's value yet so we
         * can't do any check on it.
         */
    }

    const auto& metadata = get_attributes_metadata(meta_key.objecttype);

    if (metadata.empty())
    {
        SWSS_LOG_ERROR("get attributes metadata returned empty list for object type: %d", meta_key.objecttype);

        return SAI_STATUS_FAILURE;
    }

    // check if all mandatory attributes were passed

    for (auto mdp: metadata)
    {
        const sai_attr_metadata_t& md = *mdp;

        if (!SAI_HAS_FLAG_MANDATORY_ON_CREATE(md.flags))
        {
            continue;
        }

        if (md.isconditional)
        {
            // skip conditional attributes for now
            continue;
        }

        const auto &it = attrs.find(md.attrid);

        if (it == attrs.end())
        {
            /*
             * Buffer profile shared static/dynamic is special case since it's
             * mandatory on create but condition is on
             * SAI_BUFFER_PROFILE_ATTR_POOL_ID attribute (see file saibuffer.h).
             */

            if (md.objecttype == SAI_OBJECT_TYPE_BUFFER_PROFILE &&
                    (md.attrid == SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH ||
                    (md.attrid == SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH)))
            {
                auto pool_id_attr = sai_metadata_get_attr_by_id(SAI_BUFFER_PROFILE_ATTR_POOL_ID, attr_count, attr_list);

                if (pool_id_attr == NULL)
                {
                    META_LOG_ERROR(md, "buffer pool ID is not passed when creating buffer profile, attr is mandatory");

                    return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
                }

                sai_object_id_t pool_id = pool_id_attr->value.oid;

                if (pool_id == SAI_NULL_OBJECT_ID)
                {
                    /* attribute allows null */
                    continue;
                }

                /*
                 * Object type  pool_id is correct since previous loop checked that.
                 * Now extract SAI_BUFFER_POOL_THRESHOLD_MODE attribute
                 */

                sai_object_meta_key_t mk = { .objecttype = SAI_OBJECT_TYPE_BUFFER_POOL, .objectkey = { .key = { .object_id = pool_id } } };

                auto pool_md = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_BUFFER_POOL, SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE);

                auto prev = get_object_previous_attr(mk, *pool_md);

                sai_buffer_pool_threshold_mode_t mode;

                if (prev == NULL)
                {
                    mode = (sai_buffer_pool_threshold_mode_t)pool_md->defaultvalue->s32;
                }
                else
                {
                    mode = (sai_buffer_pool_threshold_mode_t)prev->getSaiAttr()->value.s32;
                }

                if ((mode == SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC && md.attrid == SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH) ||
                    (mode == SAI_BUFFER_POOL_THRESHOLD_MODE_STATIC && md.attrid == SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH))
                {
                    /* attribute is mandatory */
                }
                else
                {
                    /* in this case attribute is not mandatory */
                    META_LOG_INFO(md, "not mandatory");
                    continue;
                }
            }

            if (md.attrid == SAI_ACL_TABLE_ATTR_FIELD_ACL_RANGE_TYPE && md.objecttype == SAI_OBJECT_TYPE_ACL_TABLE)
            {
                /*
                 * TODO Remove in future. Workaround for range type which in
                 * headers was marked as mandatory by mistake, and we need to
                 * wait for next SAI integration to pull this change in.
                 */

                META_LOG_WARN(md, "Workaround: attribute is mandatory but not passed in attr list, REMOVE ME");

                continue;
            }

            META_LOG_ERROR(md, "attribute is mandatory but not passed in attr list");

            return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
        }
    }

    // check if we need any conditional attributes
    for (auto mdp: metadata)
    {
        const sai_attr_metadata_t& md = *mdp;

        if (!md.isconditional)
        {
            continue;
        }

        // this is conditional attribute, check if it's required

        bool any = false;

        for (size_t index = 0; md.conditions[index] != NULL; index++)
        {
            const auto& c = *md.conditions[index];

            // conditions may only be on the same object type
            const auto& cmd = *sai_metadata_get_attr_metadata(meta_key.objecttype, c.attrid);

            const sai_attribute_value_t* cvalue = cmd.defaultvalue;

            const sai_attribute_t *cattr = sai_metadata_get_attr_by_id(c.attrid, attr_count, attr_list);

            if (cattr != NULL)
            {
                META_LOG_DEBUG(md, "condition attr %d was passed, using it's value", c.attrid);

                cvalue = &cattr->value;
            }

            if (cmd.attrvaluetype == SAI_ATTR_VALUE_TYPE_BOOL)
            {
                if (c.condition.booldata == cvalue->booldata)
                {
                    META_LOG_DEBUG(md, "bool condition was met on attr %d = %d", cmd.attrid, c.condition.booldata);

                    any = true;
                    break;
                }
            }
            else // enum condition
            {
                int32_t val = cvalue->s32;

                switch (cmd.attrvaluetype)
                {
                    case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
                        val = cvalue->aclfield.data.s32;
                        break;

                    case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
                        val = cvalue->aclaction.parameter.s32;
                        break;

                    default:
                        val = cvalue->s32;
                        break;
                }

                if (c.condition.s32 == val)
                {
                    META_LOG_DEBUG(md, "enum condition was met on attr id %d, val = %d", cmd.attrid, val);

                    any = true;
                    break;
                }
            }
        }

        if (!any)
        {
            // maybe we can let it go here?
            if (attrs.find(md.attrid) != attrs.end())
            {
                META_LOG_ERROR(md, "conditional, but condition was not met, this attribute is not required, but passed");

                return SAI_STATUS_INVALID_PARAMETER;
            }

            continue;
        }

        // is required, check if user passed it
        const auto &it = attrs.find(md.attrid);

        if (it == attrs.end())
        {
            META_LOG_ERROR(md, "attribute is conditional and is mandatory but not passed in attr list");

            return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
        }
    }

    if (haskeys)
    {
        std::string key = AttrKeyMap::constructKey(meta_key, attr_count, attr_list);

        // since we didn't created oid yet, we don't know if attribute key exists, check all
        if (g_attrKeys.attrKeyExists(key))
        {
            SWSS_LOG_ERROR("attribute key %s already exists, can't create", key.c_str());

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    return SAI_STATUS_SUCCESS;
}

bool meta_is_object_in_default_state(
        _In_ sai_object_id_t oid)
{
    SWSS_LOG_ENTER();

    if (oid == SAI_NULL_OBJECT_ID)
        SWSS_LOG_THROW("not expected NULL object id");

    if (!g_oids.objectReferenceExists(oid))
    {
        SWSS_LOG_WARN("object %s refrence not exists, bug!",
                sai_serialize_object_id(oid).c_str());
        return false;
    }

    sai_object_meta_key_t meta_key;

    meta_key.objecttype = sai_object_type_query(oid);
    meta_key.objectkey.key.object_id = oid;

    std::string key = sai_serialize_object_meta_key(meta_key);

    if (!g_saiObjectCollection.objectExists(key))
    {
        SWSS_LOG_WARN("object %s don't exists in local database, bug!",
                sai_serialize_object_id(oid).c_str());
        return false;
    }

    auto attrs = g_saiObjectCollection.getObject(meta_key)->getAttributes();

    for (const auto& attr: attrs)
    {
        auto &md = *attr->getSaiAttrMetadata();

        auto *a = attr->getSaiAttr();

        if (md.isreadonly)
            continue;

        if (!md.isoidattribute)
            continue;

        if (md.attrvaluetype == SAI_ATTR_VALUE_TYPE_OBJECT_ID)
        {
            if (a->value.oid != SAI_NULL_OBJECT_ID)
            {
                SWSS_LOG_ERROR("object %s has non default state on %s: %s, expected NULL",
                        sai_serialize_object_id(oid).c_str(),
                        md.attridname,
                        sai_serialize_object_id(a->value.oid).c_str());

                return false;
            }
        }
        else if (md.attrvaluetype == SAI_ATTR_VALUE_TYPE_OBJECT_LIST)
        {
            for (uint32_t i = 0; i < a->value.objlist.count; i++)
            {
                if (a->value.objlist.list[i] != SAI_NULL_OBJECT_ID)
                {
                    SWSS_LOG_ERROR("object %s has non default state on %s[%u]: %s, expected NULL",
                            sai_serialize_object_id(oid).c_str(),
                            md.attridname,
                            i,
                            sai_serialize_object_id(a->value.objlist.list[i]).c_str());

                    return false;
                }
            }
        }
        else
        {
            // unable to check whether object is in default state, need fix

            SWSS_LOG_ERROR("unsupported oid attribute: %s, FIX ME!", md.attridname);
            return false;
        }
    }

    return true;
}

sai_status_t meta_port_remove_validation(
        _In_ const sai_object_meta_key_t& meta_key)
{
    SWSS_LOG_ENTER();

    sai_object_id_t port_id = meta_key.objectkey.key.object_id;

    auto relatedObjects = g_portRelatedSet.getPortRelatedObjects(port_id);

    if (relatedObjects.size() == 0)
    {
        // user didn't query any queues, ipgs or scheduler groups
        // for this port, then we can just skip this
        return SAI_STATUS_SUCCESS;
    }

    if (g_oids.getObjectReferenceCount(port_id) != 0)
    {
        SWSS_LOG_ERROR("port %s reference count is not zero, can't remove",
                sai_serialize_object_id(port_id).c_str());

        return SAI_STATUS_OBJECT_IN_USE;
    }

    if (!meta_is_object_in_default_state(port_id))
    {
        SWSS_LOG_ERROR("port %s is not in default state, can't remove",
                sai_serialize_object_id(port_id).c_str());

        return SAI_STATUS_OBJECT_IN_USE;
    }

    for (auto oid: relatedObjects)
    {
        if (g_oids.getObjectReferenceCount(oid) != 0)
        {
            SWSS_LOG_ERROR("port %s related object %s reference count is not zero, can't remove",
                    sai_serialize_object_id(port_id).c_str(),
                    sai_serialize_object_id(oid).c_str());

            return SAI_STATUS_OBJECT_IN_USE;
        }

        if (!meta_is_object_in_default_state(oid))
        {
            SWSS_LOG_ERROR("port related object %s is not in default state, can't remove",
                    sai_serialize_object_id(oid).c_str());

            return SAI_STATUS_OBJECT_IN_USE;
        }
    }

    SWSS_LOG_NOTICE("all objects related to port %s are in default state, can be remove",
                sai_serialize_object_id(port_id).c_str());

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_generic_validation_remove(
        _In_ const sai_object_meta_key_t& meta_key)
{
    SWSS_LOG_ENTER();

    std::string key = sai_serialize_object_meta_key(meta_key);

    if (!g_saiObjectCollection.objectExists(key))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    auto info = sai_metadata_get_object_type_info(meta_key.objecttype);

    if (info->isnonobjectid)
    {
        // we don't keep reference of those since those are leafs
        return SAI_STATUS_SUCCESS;
    }

    // for OID objects check oid value

    sai_object_id_t oid = meta_key.objectkey.key.object_id;

    if (oid == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("can't remove null object id");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t object_type = sai_object_type_query(oid);

    if (object_type == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("oid 0x%" PRIx64 " is not valid, returned null object id", oid);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (object_type != meta_key.objecttype)
    {
        SWSS_LOG_ERROR("oid 0x%" PRIx64 " type %d is not accepted, expected object type %d", oid, object_type, meta_key.objecttype);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (!g_oids.objectReferenceExists(oid))
    {
        SWSS_LOG_ERROR("object 0x%" PRIx64 " reference doesn't exist", oid);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    int count = g_oids.getObjectReferenceCount(oid);

    if (count != 0)
    {
        if (object_type == SAI_OBJECT_TYPE_SWITCH)
        {
            /*
             * We allow to remove switch object even if there are ROUTE_ENTRY
             * created and referencing this switch, since remove could be used
             * in WARM boot scenario.
             */

            SWSS_LOG_WARN("removing switch object 0x%" PRIx64 " reference count is %d, removing all objects from meta DB", oid, count);

            meta_init_db();

            return SAI_STATUS_SUCCESS;
        }

        SWSS_LOG_ERROR("object 0x%" PRIx64 " reference count is %d, can't remove", oid, count);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (meta_key.objecttype == SAI_OBJECT_TYPE_PORT)
    {
        return meta_port_remove_validation(meta_key);
    }

    // should be safe to remove

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_generic_validation_set(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    if (attr == NULL)
    {
        SWSS_LOG_ERROR("attribute pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    auto mdp = sai_metadata_get_attr_metadata(meta_key.objecttype, attr->id);

    if (mdp == NULL)
    {
        SWSS_LOG_ERROR("unable to find attribute metadata %d:%d", meta_key.objecttype, attr->id);

        return SAI_STATUS_FAILURE;
    }

    const sai_attribute_value_t& value = attr->value;

    const sai_attr_metadata_t& md = *mdp;

    META_LOG_DEBUG(md, "(set)");

    if (SAI_HAS_FLAG_READ_ONLY(md.flags))
    {
        if (meta_unittests_get_and_erase_set_readonly_flag(md))
        {
            META_LOG_NOTICE(md, "readonly attribute is allowd to be set (unittests enabled)");
        }
        else
        {
            META_LOG_ERROR(md, "attr is read only and cannot be modified");

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    if (SAI_HAS_FLAG_CREATE_ONLY(md.flags))
    {
        META_LOG_ERROR(md, "attr is create only and cannot be modified");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (SAI_HAS_FLAG_KEY(md.flags))
    {
        META_LOG_ERROR(md, "attr is key and cannot be modified");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t switch_id = SAI_NULL_OBJECT_ID;

    auto info = sai_metadata_get_object_type_info(meta_key.objecttype);

    if (!info->isnonobjectid)
    {
        switch_id = sai_switch_id_query(meta_key.objectkey.key.object_id);

        if (!g_oids.objectReferenceExists(switch_id))
        {
            SWSS_LOG_ERROR("switch id 0x%" PRIx64 " doesn't exist", switch_id);
            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    switch_id = meta_extract_switch_id(meta_key, switch_id);

    // if we set OID check if exists and if type is correct

    switch (md.attrvaluetype)
    {
        case SAI_ATTR_VALUE_TYPE_BOOL:
            // case SAI_ATTR_VALUE_TYPE_CHARDATA:
        case SAI_ATTR_VALUE_TYPE_UINT8:
        case SAI_ATTR_VALUE_TYPE_INT8:
        case SAI_ATTR_VALUE_TYPE_UINT16:
        case SAI_ATTR_VALUE_TYPE_INT16:
        case SAI_ATTR_VALUE_TYPE_UINT32:
        case SAI_ATTR_VALUE_TYPE_INT32:
        case SAI_ATTR_VALUE_TYPE_UINT64:
        case SAI_ATTR_VALUE_TYPE_INT64:
        case SAI_ATTR_VALUE_TYPE_MAC:
        case SAI_ATTR_VALUE_TYPE_IPV4:
        case SAI_ATTR_VALUE_TYPE_IPV6:
        case SAI_ATTR_VALUE_TYPE_POINTER:
            // primitives
            break;

        case SAI_ATTR_VALUE_TYPE_IP_ADDRESS:

            {
                switch (value.ipaddr.addr_family)
                {
                    case SAI_IP_ADDR_FAMILY_IPV4:
                    case SAI_IP_ADDR_FAMILY_IPV6:
                        break;

                    default:

                        SWSS_LOG_ERROR("invalid address family: %d", value.ipaddr.addr_family);

                        return SAI_STATUS_INVALID_PARAMETER;
                }

                break;
            }

        case SAI_ATTR_VALUE_TYPE_OBJECT_ID:

            {

                if (md.objecttype == SAI_OBJECT_TYPE_SCHEDULER_GROUP &&
                        md.attrid == SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID &&
                        value.oid == SAI_NULL_OBJECT_ID)
                {
                    // XXX workaround, since this profile can't be NULL according to metadata,
                    // but currently on mlnx2700 null can be set, need verify and fix
                    break;
                }

                sai_status_t status = meta_generic_validation_objlist(md, switch_id, 1, &value.oid);

                if (status != SAI_STATUS_SUCCESS)
                {
                    return status;
                }

                break;
            }

        case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:

            {
                sai_status_t status = meta_generic_validation_objlist(md, switch_id, value.objlist.count, value.objlist.list);

                if (status != SAI_STATUS_SUCCESS)
                {
                    return status;
                }

                break;
            }

            // ACL FIELD

        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_BOOL:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT8:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT16:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT16:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT32:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_MAC:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV4:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV6:
            // primitives
            break;

        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:

            {
                if (!value.aclfield.enable)
                {
                    break;
                }

                sai_status_t status = meta_generic_validation_objlist(md, switch_id, 1, &value.aclfield.data.oid);

                if (status != SAI_STATUS_SUCCESS)
                {
                    return status;
                }

                break;
            }

        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:

            {
                if (!value.aclfield.enable)
                {
                    break;
                }

                sai_status_t status = meta_generic_validation_objlist(md, switch_id, value.aclfield.data.objlist.count, value.aclfield.data.objlist.list);

                if (status != SAI_STATUS_SUCCESS)
                {
                    return status;
                }

                break;
            }

            // case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8_LIST:

            // ACL ACTION

        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_BOOL:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT8:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT8:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT16:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT16:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT32:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_MAC:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV4:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV6:
            break;

        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:

            {
                if (!value.aclaction.enable)
                {
                    break;
                }

                sai_status_t status = meta_generic_validation_objlist(md, switch_id, 1, &value.aclaction.parameter.oid);

                if (status != SAI_STATUS_SUCCESS)
                {
                    return status;
                }

                break;
            }

        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:

            {
                if (!value.aclaction.enable)
                {
                    break;
                }

                sai_status_t status = meta_generic_validation_objlist(md, switch_id, value.aclaction.parameter.objlist.count, value.aclaction.parameter.objlist.list);

                if (status != SAI_STATUS_SUCCESS)
                {
                    return status;
                }

                break;
            }

            // ACL END

        case SAI_ATTR_VALUE_TYPE_UINT8_LIST:
            VALIDATION_LIST(md, value.u8list);
            break;
        case SAI_ATTR_VALUE_TYPE_INT8_LIST:
            VALIDATION_LIST(md, value.s8list);
            break;
        case SAI_ATTR_VALUE_TYPE_UINT16_LIST:
            VALIDATION_LIST(md, value.u16list);
            break;
        case SAI_ATTR_VALUE_TYPE_INT16_LIST:
            VALIDATION_LIST(md, value.s16list);
            break;
        case SAI_ATTR_VALUE_TYPE_UINT32_LIST:
            VALIDATION_LIST(md, value.u32list);
            break;
        case SAI_ATTR_VALUE_TYPE_INT32_LIST:
            VALIDATION_LIST(md, value.s32list);
            break;
        case SAI_ATTR_VALUE_TYPE_QOS_MAP_LIST:
            VALIDATION_LIST(md, value.qosmap);
            break;
        case SAI_ATTR_VALUE_TYPE_ACL_RESOURCE_LIST:
            VALIDATION_LIST(md, value.aclresource);
            break;
        case SAI_ATTR_VALUE_TYPE_IP_ADDRESS_LIST:
            VALIDATION_LIST(md, value.ipaddrlist);
            break;

        case SAI_ATTR_VALUE_TYPE_UINT32_RANGE:

            if (value.u32range.min > value.u32range.max)
            {
                META_LOG_ERROR(md, "invalid range %u .. %u", value.u32range.min, value.u32range.max);

                return SAI_STATUS_INVALID_PARAMETER;
            }

            break;

        case SAI_ATTR_VALUE_TYPE_INT32_RANGE:

            if (value.s32range.min > value.s32range.max)
            {
                META_LOG_ERROR(md, "invalid range %u .. %u", value.s32range.min, value.s32range.max);

                return SAI_STATUS_INVALID_PARAMETER;
            }

            break;

        case SAI_ATTR_VALUE_TYPE_IP_PREFIX:

                {
                    switch (value.ipprefix.addr_family)
                    {
                        case SAI_IP_ADDR_FAMILY_IPV4:
                        case SAI_IP_ADDR_FAMILY_IPV6:
                            break;

                        default:

                            SWSS_LOG_ERROR("invalid address family: %d", value.ipprefix.addr_family);

                            return SAI_STATUS_INVALID_PARAMETER;
                    }

                    break;
                }

        case SAI_ATTR_VALUE_TYPE_ACL_CAPABILITY:
            VALIDATION_LIST(md, value.aclcapability.action_list);
            break;

        default:

            META_LOG_THROW(md, "serialization type is not supported yet FIXME");
    }

    if (md.isenum)
    {
        int32_t val = value.s32;

        switch (md.attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
                val = value.aclfield.data.s32;
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
                val = value.aclaction.parameter.s32;
                break;

            default:
                val = value.s32;
                break;
        }

        if (!sai_metadata_is_allowed_enum_value(&md, val))
        {
            META_LOG_ERROR(md, "is enum, but value %d not found on allowed values list", val);

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    if (md.isenumlist)
    {
        // we allow repeats on enum list
        if (value.s32list.count != 0 && value.s32list.list == NULL)
        {
            META_LOG_ERROR(md, "enum list is NULL");

            return SAI_STATUS_INVALID_PARAMETER;
        }

        for (uint32_t i = value.s32list.count; i < value.s32list.count; ++i)
        {
            int32_t s32 = value.s32list.list[i];

            if (!sai_metadata_is_allowed_enum_value(&md, s32))
            {
                SWSS_LOG_ERROR("is enum list, but value %d not found on allowed values list", s32);

                return SAI_STATUS_INVALID_PARAMETER;
            }
        }
    }

    if (md.isconditional)
    {
        // check if it was set on local DB
        // (this will not respect create_only with default)

        if (get_object_previous_attr(meta_key, md) == NULL)
        {
            std::string key = sai_serialize_object_meta_key(meta_key);

            META_LOG_WARN(md, "set for conditional, but not found in local db, object %s created on switch ?", key.c_str());
        }
        else
        {
            META_LOG_DEBUG(md, "conditional attr found in local db");
        }

        META_LOG_DEBUG(md, "conditional attr found in local db");
    }

    // check if object on which we perform operation exists

    std::string key = sai_serialize_object_meta_key(meta_key);

    if (!g_saiObjectCollection.objectExists(key))
    {
        META_LOG_ERROR(md, "object key %s doesn't exist", key.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // object exists in DB so we can do "set" operation

    if (info->isnonobjectid)
    {
        SWSS_LOG_DEBUG("object key exists: %s", key.c_str());
    }
    else
    {
        /*
         * Check if object we are calling SET is the same object type as the
         * type of SET function.
         */

        sai_object_id_t oid = meta_key.objectkey.key.object_id;

        sai_object_type_t object_type = sai_object_type_query(oid);

        if (object_type == SAI_NULL_OBJECT_ID)
        {
            META_LOG_ERROR(md, "oid 0x%" PRIx64 " is not valid, returned null object id", oid);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (object_type != meta_key.objecttype)
        {
            META_LOG_ERROR(md, "oid 0x%" PRIx64 " type %d is not accepted, expected object type %d", oid, object_type, meta_key.objecttype);

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_generic_validation_get(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ const uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    if (attr_count < 1)
    {
        SWSS_LOG_ERROR("expected at least 1 attribute when calling get, zero given");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (attr_count > MAX_LIST_COUNT)
    {
        SWSS_LOG_ERROR("get attribute count %u > max list count %u", attr_count, MAX_LIST_COUNT);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (attr_list == NULL)
    {
        SWSS_LOG_ERROR("attribute list pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    SWSS_LOG_DEBUG("attr count = %u", attr_count);

    for (uint32_t i = 0; i < attr_count; ++i)
    {
        const sai_attribute_t* attr = &attr_list[i];

        auto mdp = sai_metadata_get_attr_metadata(meta_key.objecttype, attr->id);

        if (mdp == NULL)
        {
            SWSS_LOG_ERROR("unable to find attribute metadata %d:%d", meta_key.objecttype, attr->id);

            return SAI_STATUS_FAILURE;
        }

        const sai_attribute_value_t& value = attr->value;

        const sai_attr_metadata_t& md = *mdp;

        META_LOG_DEBUG(md, "(get)");

        if (md.isconditional)
        {
            /*
             * XXX workaround
             *
             * TODO If object was created internally by switch (like bridge
             * port) then current db will not have previous value of this
             * attribute (like SAI_BRIDGE_PORT_ATTR_PORT_ID) or even other oid.
             * This can lead to inconsistency, that we queried one oid, and its
             * attribute also oid, and then did a "set" on that value, and now
             * reference is not decreased since previous oid was not snooped.
             *
             * TODO This concern all attributes not only conditionals
             *
             * If attribute is conditional, we need to check if condition is
             * met, if not then this attribute is not mandatory so we can
             * return fail in that case, for that we need all internal
             * switch objects after create.
             */

            // check if it was set on local DB
            // (this will not respect create_only with default)
            if (get_object_previous_attr(meta_key, md) == NULL)
            {
                std::string key = sai_serialize_object_meta_key(meta_key);

                // XXX produces too much noise
                // META_LOG_WARN(md, "get for conditional, but not found in local db, object %s created on switch ?", key.c_str());
            }
            else
            {
                META_LOG_DEBUG(md, "conditional attr found in local db");
            }
        }

        /*
         * When GET api is performed, later on same methods serialize/deserialize
         * are used for create/set/get apis. User may not clear input attributes
         * buffer (since it is in/out for example for lists) and in case of
         * values that are validated like "enum" it will try to find best
         * match for enum, and if not found, it will print warning message.
         *
         * In this place we can clear user buffer, so when it will go to
         * serialize method it will pick first enum on the list.
         *
         * For primitive attributes we could just set entire attribute value to zero.
         */

        if (md.isenum)
        {
            attr_list[i].value.s32 = 0;
        }

        switch (md.attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_BOOL:
            case SAI_ATTR_VALUE_TYPE_CHARDATA:
            case SAI_ATTR_VALUE_TYPE_UINT8:
            case SAI_ATTR_VALUE_TYPE_INT8:
            case SAI_ATTR_VALUE_TYPE_UINT16:
            case SAI_ATTR_VALUE_TYPE_INT16:
            case SAI_ATTR_VALUE_TYPE_UINT32:
            case SAI_ATTR_VALUE_TYPE_INT32:
            case SAI_ATTR_VALUE_TYPE_UINT64:
            case SAI_ATTR_VALUE_TYPE_INT64:
            case SAI_ATTR_VALUE_TYPE_MAC:
            case SAI_ATTR_VALUE_TYPE_IPV4:
            case SAI_ATTR_VALUE_TYPE_IPV6:
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS:
            case SAI_ATTR_VALUE_TYPE_POINTER:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:
                VALIDATION_LIST(md, value.objlist);
                break;

            case SAI_ATTR_VALUE_TYPE_VLAN_LIST:

                {
                    if (value.vlanlist.count == 0 && value.vlanlist.list != NULL)
                    {
                        META_LOG_ERROR(md, "vlan list count is zero, but list not NULL");

                        return SAI_STATUS_INVALID_PARAMETER;
                    }

                    if (value.vlanlist.count != 0 && value.vlanlist.list == NULL)
                    {
                        META_LOG_ERROR(md, "vlan list count is %u, but list is NULL", value.vlanlist.count);

                        return SAI_STATUS_INVALID_PARAMETER;
                    }

                    if (value.vlanlist.count > MAXIMUM_VLAN_NUMBER)
                    {
                        META_LOG_ERROR(md, "vlan count is too big %u > %u", value.vlanlist.count, MAXIMUM_VLAN_NUMBER);

                        return SAI_STATUS_INVALID_PARAMETER;
                    }

                    break;
                }

                // ACL FIELD

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV6:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:
                VALIDATION_LIST(md, value.aclfield.data.objlist);
                break;

                // case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8_LIST:

                // ACL ACTION

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV6:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:
                VALIDATION_LIST(md, value.aclaction.parameter.objlist);
                break;

                // ACL END

            case SAI_ATTR_VALUE_TYPE_UINT8_LIST:
                VALIDATION_LIST(md, value.u8list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT8_LIST:
                VALIDATION_LIST(md, value.s8list);
                break;
            case SAI_ATTR_VALUE_TYPE_UINT16_LIST:
                VALIDATION_LIST(md, value.u16list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT16_LIST:
                VALIDATION_LIST(md, value.s16list);
                break;
            case SAI_ATTR_VALUE_TYPE_UINT32_LIST:
                VALIDATION_LIST(md, value.u32list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT32_LIST:
                VALIDATION_LIST(md, value.s32list);
                break;
            case SAI_ATTR_VALUE_TYPE_QOS_MAP_LIST:
                VALIDATION_LIST(md, value.qosmap);
                break;
            case SAI_ATTR_VALUE_TYPE_ACL_RESOURCE_LIST:
                VALIDATION_LIST(md, value.aclresource);
                break;
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS_LIST:
                VALIDATION_LIST(md, value.ipaddrlist);
                break;

            case SAI_ATTR_VALUE_TYPE_UINT32_RANGE:
            case SAI_ATTR_VALUE_TYPE_INT32_RANGE:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_CAPABILITY:
                VALIDATION_LIST(md, value.aclcapability.action_list);
                break;

            default:

                // acl capability will is more complex since is in/out we need to check stage

                META_LOG_THROW(md, "serialization type is not supported yet FIXME");
        }
    }

    std::string key = sai_serialize_object_meta_key(meta_key);

    if (!g_saiObjectCollection.objectExists(key))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    auto info = sai_metadata_get_object_type_info(meta_key.objecttype);

    if (info->isnonobjectid)
    {
        SWSS_LOG_DEBUG("object key exists: %s", key.c_str());
    }
    else
    {
        /*
         * Check if object we are calling GET is the same object type as the
         * type of GET function.
         */

        sai_object_id_t oid = meta_key.objectkey.key.object_id;

        sai_object_type_t object_type = sai_object_type_query(oid);

        if (object_type == SAI_NULL_OBJECT_ID)
        {
            SWSS_LOG_ERROR("oid 0x%" PRIx64 " is not valid, returned null object id", oid);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (object_type != meta_key.objecttype)
        {
            SWSS_LOG_ERROR("oid 0x%" PRIx64 " type %d is not accepted, expected object type %d", oid, object_type, meta_key.objecttype);

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    // object exists in DB so we can do "get" operation

    return SAI_STATUS_SUCCESS;
}

bool warmBoot = false;

void meta_warm_boot_notify()
{
    SWSS_LOG_ENTER();

    warmBoot = true;

    SWSS_LOG_NOTICE("warmBoot = true");
}

void meta_generic_validation_post_create(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_object_id_t switch_id,
        _In_ const uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    std::string key = sai_serialize_object_meta_key(meta_key);

    if (g_saiObjectCollection.objectExists(key))
    {
        if (warmBoot && meta_key.objecttype == SAI_OBJECT_TYPE_SWITCH)
        {
            SWSS_LOG_NOTICE("post switch create after WARM BOOT");
        }
        else
        {
            SWSS_LOG_ERROR("object key %s already exists (vendor bug?)", key.c_str());

            // this may produce inconsistency
        }
    }

    if (warmBoot && meta_key.objecttype == SAI_OBJECT_TYPE_SWITCH)
    {
        SWSS_LOG_NOTICE("skipping create switch on WARM BOOT since it was already created");
    }
    else
    {
        g_saiObjectCollection.createObject(meta_key);
    }

    auto info = sai_metadata_get_object_type_info(meta_key.objecttype);

    if (info->isnonobjectid)
    {
        /*
         * Increase object reference count for all object ids in non object id
         * members.
         */

        for (size_t j = 0; j < info->structmemberscount; ++j)
        {
            const sai_struct_member_info_t *m = info->structmembers[j];

            if (m->membervaluetype != SAI_ATTR_VALUE_TYPE_OBJECT_ID)
            {
                continue;
            }

            g_oids.objectReferenceIncrement(m->getoid(&meta_key));
        }
    }
    else
    {
        /*
         * Check if object created was expected type as the type of CRATE
         * function.
         */

        do
        {
            sai_object_id_t oid = meta_key.objectkey.key.object_id;

            if (oid == SAI_NULL_OBJECT_ID)
            {
                SWSS_LOG_ERROR("created oid is null object id (vendor bug?)");
                break;
            }

            sai_object_type_t object_type = sai_object_type_query(oid);

            if (object_type == SAI_NULL_OBJECT_ID)
            {
                SWSS_LOG_ERROR("created oid 0x%" PRIx64 " is not valid object type after create (null) (vendor bug?)", oid);
                break;
            }

            if (object_type != meta_key.objecttype)
            {
                SWSS_LOG_ERROR("created oid 0x%" PRIx64 " type %s, expected %s (vendor bug?)",
                        oid,
                        sai_serialize_object_type(object_type).c_str(),
                        sai_serialize_object_type(meta_key.objecttype).c_str());
                break;
            }

            if (meta_key.objecttype != SAI_OBJECT_TYPE_SWITCH)
            {
                /*
                 * Check if created object switch is the same as input switch.
                 */

                sai_object_id_t query_switch_id = sai_switch_id_query(meta_key.objectkey.key.object_id);

                if (!g_oids.objectReferenceExists(query_switch_id))
                {
                    SWSS_LOG_ERROR("switch id 0x%" PRIx64 " doesn't exist", query_switch_id);
                    break;
                }

                if (switch_id != query_switch_id)
                {
                    SWSS_LOG_ERROR("created oid 0x%" PRIx64 " switch id 0x%" PRIx64 " is different than requested 0x%" PRIx64 "",
                            oid, query_switch_id, switch_id);
                    break;
                }
            }

            if (warmBoot && meta_key.objecttype == SAI_OBJECT_TYPE_SWITCH)
            {
                SWSS_LOG_NOTICE("skip insert switch reference insert in WARM_BOOT");
            }
            else
            {
                g_oids.objectReferenceInsert(oid);
            }

        } while (false);
    }

    if (warmBoot)
    {
        SWSS_LOG_NOTICE("warmBoot = false");

        warmBoot = false;
    }

    bool haskeys;

    for (uint32_t idx = 0; idx < attr_count; ++idx)
    {
        const sai_attribute_t* attr = &attr_list[idx];

        auto mdp = sai_metadata_get_attr_metadata(meta_key.objecttype, attr->id);

        const sai_attribute_value_t& value = attr->value;

        const sai_attr_metadata_t& md = *mdp;

        if (SAI_HAS_FLAG_KEY(md.flags))
        {
            haskeys = true;
            META_LOG_DEBUG(md, "attr is key");
        }

        // increase reference on object id types

        switch (md.attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_BOOL:
            case SAI_ATTR_VALUE_TYPE_CHARDATA:
            case SAI_ATTR_VALUE_TYPE_UINT8:
            case SAI_ATTR_VALUE_TYPE_INT8:
            case SAI_ATTR_VALUE_TYPE_UINT16:
            case SAI_ATTR_VALUE_TYPE_INT16:
            case SAI_ATTR_VALUE_TYPE_UINT32:
            case SAI_ATTR_VALUE_TYPE_INT32:
            case SAI_ATTR_VALUE_TYPE_UINT64:
            case SAI_ATTR_VALUE_TYPE_INT64:
            case SAI_ATTR_VALUE_TYPE_MAC:
            case SAI_ATTR_VALUE_TYPE_IPV4:
            case SAI_ATTR_VALUE_TYPE_IPV6:
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS:
            case SAI_ATTR_VALUE_TYPE_IP_PREFIX:
            case SAI_ATTR_VALUE_TYPE_POINTER:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:
                g_oids.objectReferenceIncrement(value.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:
                g_oids.objectReferenceIncrement(value.objlist);
                break;

            case SAI_ATTR_VALUE_TYPE_VLAN_LIST:
                break;

                // ACL FIELD

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV6:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:
                if (value.aclfield.enable)
                {
                    g_oids.objectReferenceIncrement(value.aclfield.data.oid);
                }
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:
                if (value.aclfield.enable)
                {
                    g_oids.objectReferenceIncrement(value.aclfield.data.objlist);
                }
                break;

                // case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8_LIST:

                // ACL ACTION

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV6:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:
                if (value.aclaction.enable)
                {
                    g_oids.objectReferenceIncrement(value.aclaction.parameter.oid);
                }
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:
                if (value.aclaction.enable)
                {
                    g_oids.objectReferenceIncrement(value.aclaction.parameter.objlist);
                }
                break;

                // ACL END

            case SAI_ATTR_VALUE_TYPE_UINT8_LIST:
            case SAI_ATTR_VALUE_TYPE_INT8_LIST:
            case SAI_ATTR_VALUE_TYPE_UINT16_LIST:
            case SAI_ATTR_VALUE_TYPE_INT16_LIST:
            case SAI_ATTR_VALUE_TYPE_UINT32_LIST:
            case SAI_ATTR_VALUE_TYPE_INT32_LIST:
            case SAI_ATTR_VALUE_TYPE_QOS_MAP_LIST:
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS_LIST:
            case SAI_ATTR_VALUE_TYPE_UINT32_RANGE:
            case SAI_ATTR_VALUE_TYPE_INT32_RANGE:
            case SAI_ATTR_VALUE_TYPE_ACL_RESOURCE_LIST:
                // no special action required
                break;

            default:

                META_LOG_THROW(md, "serialization type is not supported yet FIXME");
        }

        g_saiObjectCollection.setObjectAttr(meta_key, md, attr);
    }

    if (haskeys)
    {
        auto mKey = sai_serialize_object_meta_key(meta_key);

        auto attrKey = AttrKeyMap::constructKey(meta_key, attr_count, attr_list);

        g_attrKeys.insert(mKey, attrKey);
    }
}

void meta_generic_validation_post_remove(
        _In_ const sai_object_meta_key_t& meta_key);

void post_port_remove(
        _In_ const sai_object_meta_key_t& meta_key)
{
    SWSS_LOG_ENTER();

    sai_object_id_t port_id = meta_key.objectkey.key.object_id;

    auto relatedObjects = g_portRelatedSet.getPortRelatedObjects(port_id);

    if (relatedObjects.size() == 0)
    {
        // user didn't query any queues, ipgs or scheduler groups
        // for this port, then we can just skip this

        return;
    }

    for (auto oid: relatedObjects)
    {
        // to remove existing objects, let's just call post remove for them
        // and metadata will take the rest

        sai_object_meta_key_t meta;

        meta.objecttype = sai_object_type_query(oid);
        meta.objectkey.key.object_id = oid;

        SWSS_LOG_INFO("attempt to remove port related object: %s: %s",
                sai_serialize_object_type(meta.objecttype).c_str(),
                sai_serialize_object_id(oid).c_str());

        meta_generic_validation_post_remove(meta);
    }

    // all related objects were removed, we need to clear current state

    g_portRelatedSet.removePort(port_id);

    SWSS_LOG_NOTICE("success executing post port remove actions: %s",
            sai_serialize_object_id(port_id).c_str());
}

void meta_generic_validation_post_remove(
        _In_ const sai_object_meta_key_t& meta_key)
{
    SWSS_LOG_ENTER();

    if (meta_key.objecttype == SAI_OBJECT_TYPE_SWITCH)
    {
        /*
         * If switch object was removed then meta db was cleared and there are
         * no other attributes, no need for reference counting.
         *
         * TODO we can have multiple switches, then we need to remove
         * objects associated with specific switch!
         */

        return;
    }

    // get all attributes that was set

    for (auto&it: g_saiObjectCollection.getObject(meta_key)->getAttributes())
    {
        const sai_attribute_t* attr = it->getSaiAttr();

        auto mdp = sai_metadata_get_attr_metadata(meta_key.objecttype, attr->id);

        const sai_attribute_value_t& value = attr->value;

        const sai_attr_metadata_t& md = *mdp;

        // decrease reference on object id types

        switch (md.attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_BOOL:
            case SAI_ATTR_VALUE_TYPE_CHARDATA:
            case SAI_ATTR_VALUE_TYPE_UINT8:
            case SAI_ATTR_VALUE_TYPE_INT8:
            case SAI_ATTR_VALUE_TYPE_UINT16:
            case SAI_ATTR_VALUE_TYPE_INT16:
            case SAI_ATTR_VALUE_TYPE_UINT32:
            case SAI_ATTR_VALUE_TYPE_INT32:
            case SAI_ATTR_VALUE_TYPE_UINT64:
            case SAI_ATTR_VALUE_TYPE_INT64:
            case SAI_ATTR_VALUE_TYPE_MAC:
            case SAI_ATTR_VALUE_TYPE_IPV4:
            case SAI_ATTR_VALUE_TYPE_IPV6:
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS:
            case SAI_ATTR_VALUE_TYPE_IP_PREFIX:
            case SAI_ATTR_VALUE_TYPE_POINTER:
                // primitives, ok
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:
                g_oids.objectReferenceDecrement(value.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:
                g_oids.objectReferenceDecrement(value.objlist);
                break;

                // ACL FIELD

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV6:
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:
                if (value.aclfield.enable)
                {
                    g_oids.objectReferenceDecrement(value.aclfield.data.oid);
                }
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:
                if (value.aclfield.enable)
                {
                    g_oids.objectReferenceDecrement(value.aclfield.data.objlist);
                }
                break;

                // case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8_LIST:

                // ACL ACTION

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV6:
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:
                if (value.aclaction.enable)
                {
                    g_oids.objectReferenceDecrement(value.aclaction.parameter.oid);
                }
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:
                if (value.aclaction.enable)
                {
                    g_oids.objectReferenceDecrement(value.aclaction.parameter.objlist);
                }
                break;

                // ACL END

            case SAI_ATTR_VALUE_TYPE_UINT8_LIST:
            case SAI_ATTR_VALUE_TYPE_INT8_LIST:
            case SAI_ATTR_VALUE_TYPE_UINT16_LIST:
            case SAI_ATTR_VALUE_TYPE_INT16_LIST:
            case SAI_ATTR_VALUE_TYPE_UINT32_LIST:
            case SAI_ATTR_VALUE_TYPE_INT32_LIST:
            case SAI_ATTR_VALUE_TYPE_QOS_MAP_LIST:
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS_LIST:
            case SAI_ATTR_VALUE_TYPE_UINT32_RANGE:
            case SAI_ATTR_VALUE_TYPE_INT32_RANGE:
            case SAI_ATTR_VALUE_TYPE_ACL_RESOURCE_LIST:
                // no special action required
                break;

            default:
                META_LOG_THROW(md, "serialization type is not supported yet FIXME");
        }
    }

    // we don't keep track of fdb, neighbor, route since
    // those are safe to remove any time (leafs)

    auto info = sai_metadata_get_object_type_info(meta_key.objecttype);

    if (info->isnonobjectid)
    {
        /*
         * Decrease object reference count for all object ids in non object id
         * members.
         */

        for (size_t j = 0; j < info->structmemberscount; ++j)
        {
            const sai_struct_member_info_t *m = info->structmembers[j];

            if (m->membervaluetype != SAI_ATTR_VALUE_TYPE_OBJECT_ID)
            {
                continue;
            }

            g_oids.objectReferenceDecrement(m->getoid(&meta_key));
        }
    }
    else
    {
        g_oids.objectReferenceRemove(meta_key.objectkey.key.object_id);
    }

    g_saiObjectCollection.removeObject(meta_key);

    std::string metaKey = sai_serialize_object_meta_key(meta_key);

    g_attrKeys.eraseMetaKey(metaKey);

    if (meta_key.objecttype == SAI_OBJECT_TYPE_PORT)
    {
        post_port_remove(meta_key);
    }
}

void meta_generic_validation_post_set(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    auto mdp = sai_metadata_get_attr_metadata(meta_key.objecttype, attr->id);

    const sai_attribute_value_t& value = attr->value;

    const sai_attr_metadata_t& md = *mdp;

    /*
     * TODO We need to get previous value and make deal with references, check
     * if there is default value and if it's const.
     */

    if (!SAI_HAS_FLAG_READ_ONLY(md.flags) && md.allowedobjecttypeslength) // md.isoidattribute)
    {
        if ((get_object_previous_attr(meta_key, md) == NULL) &&
                (md.defaultvaluetype != SAI_DEFAULT_VALUE_TYPE_CONST &&
                 md.defaultvaluetype != SAI_DEFAULT_VALUE_TYPE_EMPTY_LIST))
        {
            /*
             * If default value type will be internal then we should warn.
             */

            std::string key = sai_serialize_object_meta_key(meta_key);

            // XXX produces too much noise
            // META_LOG_WARN(md, "post set, not in local db, FIX snoop!: %s", key.c_str());
        }
    }

    switch (md.attrvaluetype)
    {
        case SAI_ATTR_VALUE_TYPE_BOOL:
        case SAI_ATTR_VALUE_TYPE_CHARDATA:
        case SAI_ATTR_VALUE_TYPE_UINT8:
        case SAI_ATTR_VALUE_TYPE_INT8:
        case SAI_ATTR_VALUE_TYPE_UINT16:
        case SAI_ATTR_VALUE_TYPE_INT16:
        case SAI_ATTR_VALUE_TYPE_UINT32:
        case SAI_ATTR_VALUE_TYPE_INT32:
        case SAI_ATTR_VALUE_TYPE_UINT64:
        case SAI_ATTR_VALUE_TYPE_INT64:
        case SAI_ATTR_VALUE_TYPE_MAC:
        case SAI_ATTR_VALUE_TYPE_IPV4:
        case SAI_ATTR_VALUE_TYPE_IPV6:
        case SAI_ATTR_VALUE_TYPE_POINTER:
        case SAI_ATTR_VALUE_TYPE_IP_ADDRESS:
        case SAI_ATTR_VALUE_TYPE_IP_PREFIX:
            // primitives, ok
            break;

        case SAI_ATTR_VALUE_TYPE_OBJECT_ID:

            {
                auto prev = get_object_previous_attr(meta_key, md);

                if (prev != NULL)
                {
                    // decrease previous if it was set
                    g_oids.objectReferenceDecrement(prev->getSaiAttr()->value.oid);
                }

                g_oids.objectReferenceIncrement(value.oid);

                break;
            }

        case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:

            {
                auto prev = get_object_previous_attr(meta_key, md);

                if (prev != NULL)
                {
                    // decrease previous if it was set
                    g_oids.objectReferenceDecrement(prev->getSaiAttr()->value.objlist);
                }

                g_oids.objectReferenceIncrement(value.objlist);

                break;
            }

            // case SAI_ATTR_VALUE_TYPE_VLAN_LIST:
            // will require increase vlan references

            // ACL FIELD

        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_BOOL:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT8:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT16:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT16:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT32:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_MAC:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV4:
        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV6:
            break;

        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:

            {
                auto prev = get_object_previous_attr(meta_key, md);

                if (prev)
                {
                    // decrease previous if it was set
                    if (prev->getSaiAttr()->value.aclfield.enable)
                        g_oids.objectReferenceDecrement(prev->getSaiAttr()->value.aclfield.data.oid);
                }

                if (value.aclfield.enable)
                    g_oids.objectReferenceIncrement(value.aclfield.data.oid);

                break;
            }

        case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:

            {
                auto prev = get_object_previous_attr(meta_key, md);

                if (prev)
                {
                    // decrease previous if it was set
                    if (prev->getSaiAttr()->value.aclfield.enable)
                        g_oids.objectReferenceDecrement(prev->getSaiAttr()->value.aclfield.data.objlist);
                }

                if (value.aclfield.enable)
                    g_oids.objectReferenceIncrement(value.aclfield.data.objlist);

                break;
            }

            // case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8_LIST:

            // ACL ACTION

        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_BOOL:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT8:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT8:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT16:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT16:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT32:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_MAC:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV4:
        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV6:
            // primitives
            break;

        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:

            {
                auto prev = get_object_previous_attr(meta_key, md);

                if (prev)
                {
                    // decrease previous if it was set
                    if (prev->getSaiAttr()->value.aclaction.enable)
                        g_oids.objectReferenceDecrement(prev->getSaiAttr()->value.aclaction.parameter.oid);
                }

                if (value.aclaction.enable)
                    g_oids.objectReferenceIncrement(value.aclaction.parameter.oid);
                break;
            }

        case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:

            {
                auto prev = get_object_previous_attr(meta_key, md);

                if (prev)
                {
                    // decrease previous if it was set
                    if (prev->getSaiAttr()->value.aclaction.enable)
                        g_oids.objectReferenceDecrement(prev->getSaiAttr()->value.aclaction.parameter.objlist);
                }

                if (value.aclaction.enable)
                    g_oids.objectReferenceIncrement(value.aclaction.parameter.objlist);

                break;
            }

            // ACL END

        case SAI_ATTR_VALUE_TYPE_UINT8_LIST:
        case SAI_ATTR_VALUE_TYPE_INT8_LIST:
        case SAI_ATTR_VALUE_TYPE_UINT16_LIST:
        case SAI_ATTR_VALUE_TYPE_INT16_LIST:
        case SAI_ATTR_VALUE_TYPE_UINT32_LIST:
        case SAI_ATTR_VALUE_TYPE_INT32_LIST:
        case SAI_ATTR_VALUE_TYPE_QOS_MAP_LIST:
        case SAI_ATTR_VALUE_TYPE_IP_ADDRESS_LIST:
        case SAI_ATTR_VALUE_TYPE_UINT32_RANGE:
        case SAI_ATTR_VALUE_TYPE_INT32_RANGE:
        case SAI_ATTR_VALUE_TYPE_ACL_RESOURCE_LIST:
        case SAI_ATTR_VALUE_TYPE_ACL_CAPABILITY:
            // no special action required
            break;

        default:
            META_LOG_THROW(md, "serialization type is not supported yet FIXME");
    }

    // only on create we need to increase entry object types members
    // save actual attributes and values to local db

    g_saiObjectCollection.setObjectAttr(meta_key, md, attr);
}

void meta_generic_validation_post_get_objlist(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ const sai_attr_metadata_t& md,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t count,
        _In_ const sai_object_id_t* list)
{
    SWSS_LOG_ENTER();

    /*
     * TODO This is not good enough when object was created by switch
     * internally and it have oid attributes, we need to insert them to local
     * db and increase reference count if object don't exist.
     *
     * Also this function maybe not best place to do it since it's not executed
     * when we doing get on acl field/action. But none of those are created
     * internally by switch.
     *
     * TODO Similar stuff is with SET, when we will set oid object on existing
     * switch object, but we will not have it's previous value.  We can check
     * whether default value is present and it's const NULL.
     */

    if (!SAI_HAS_FLAG_READ_ONLY(md.flags) && md.allowedobjecttypeslength) // md.isoidattribute)
    {
        if (get_object_previous_attr(meta_key, md) == NULL)
        {
            std::string key = sai_serialize_object_meta_key(meta_key);

            // XXX produces too much noise
            // META_LOG_WARN(md, "post get, not in local db, FIX snoop!: %s", key.c_str());
        }
    }

    if (count > MAX_LIST_COUNT)
    {
        META_LOG_ERROR(md, "returned get object list count %u > max list count %u", count, MAX_LIST_COUNT);
    }

    if (list == NULL)
    {
        // query was for length
        return;
    }

    std::set<sai_object_id_t> oids;

    for (uint32_t i = 0; i < count; ++i)
    {
        sai_object_id_t oid = list[i];

        if (oids.find(oid) != oids.end())
        {
            META_LOG_ERROR(md, "returned get object on list [%u] is duplicated, but not allowed", i);
            continue;
        }

        oids.insert(oid);

        if (oid == SAI_NULL_OBJECT_ID)
        {
            if (md.allownullobjectid)
            {
                // ok, null object is allowed
                continue;
            }

            META_LOG_ERROR(md, "returned get object on list [%u] is NULL, but not allowed", i);
            continue;
        }

        sai_object_type_t ot = sai_object_type_query(oid);

        if (ot == SAI_OBJECT_TYPE_NULL)
        {
            META_LOG_ERROR(md, "returned get object on list [%u] oid 0x%" PRIx64 " is not valid, returned null object type", i, oid);
            continue;
        }

        if (!sai_metadata_is_allowed_object_type(&md, ot))
        {
            META_LOG_ERROR(md, "returned get object on list [%u] oid 0x%" PRIx64 " object type %d is not allowed on this attribute", i, oid, ot);
        }

        if (!g_oids.objectReferenceExists(oid))
        {
            // NOTE: there may happen that user will request multiple object lists
            // and first list was retrieved ok, but second failed with overflow
            // then we may forget to snoop

            META_LOG_INFO(md, "returned get object on list [%u] oid 0x%" PRIx64 " object type %d does not exists in local DB (snoop)", i, oid, ot);

            sai_object_meta_key_t key = { .objecttype = ot, .objectkey = { .key = { .object_id = oid } } };

            g_oids.objectReferenceInsert(oid);

            if (!g_saiObjectCollection.objectExists(key))
            {
                g_saiObjectCollection.createObject(key);
            }
        }

        sai_object_id_t query_switch_id = sai_switch_id_query(oid);

        if (!g_oids.objectReferenceExists(query_switch_id))
        {
            SWSS_LOG_ERROR("switch id 0x%" PRIx64 " doesn't exist", query_switch_id);
        }

        if (query_switch_id != switch_id)
        {
            SWSS_LOG_ERROR("oid 0x%" PRIx64 " is from switch 0x%" PRIx64 " but expected switch 0x%" PRIx64 "", oid, query_switch_id, switch_id);
        }
    }
}

#define VALIDATION_LIST_GET(md, list) \
{\
    if (list.count > MAX_LIST_COUNT)\
    {\
        META_LOG_ERROR(md, "list count %u > max list count %u", list.count, MAX_LIST_COUNT);\
    }\
}

void meta_add_port_to_related_map(
        _In_ sai_object_id_t port_id,
        _In_ const sai_object_list_t& list)
{
    SWSS_LOG_ENTER();

    for (uint32_t i = 0; i < list.count; i++)
    {
        sai_object_id_t rel = list.list[i];

        if (rel == SAI_NULL_OBJECT_ID)
            SWSS_LOG_THROW("not expected NULL oid on the list");

        g_portRelatedSet.insert(port_id, rel);
    }
}

void meta_post_port_get(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_object_id_t switch_id,
        _In_ const uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    /*
     * User may or may not query one of below attributes to get some port
     * objects, and those objects are special since when user decide to remove
     * port, then those object will be removed automatically by vendor SAI, and
     * this action needs to be reflected here too, so if user will remove port,
     * those objects would need to be remove from local database too.
     *
     * TODO: There will be issue here, since we need to know which of those
     * objects are user created, for example if user will create some extra
     * queues with specific port, and then query queues list, those extra
     * queues would need to be explicitly removed first by user, otherwise this
     * logic here will also consider those user created queues as switch
     * default, and it will remove them when port will be removed.  Such action
     * should be prevented.
     */

    const sai_object_id_t port_id = meta_key.objectkey.key.object_id;

    for (uint32_t idx = 0; idx < attr_count; ++idx)
    {
        const sai_attribute_t& attr = attr_list[idx];

        auto& md = *sai_metadata_get_attr_metadata(meta_key.objecttype, attr.id);

        switch (md.attrid)
        {
            case SAI_PORT_ATTR_QOS_QUEUE_LIST:
            case SAI_PORT_ATTR_QOS_SCHEDULER_GROUP_LIST:
            case SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST:
                meta_add_port_to_related_map(port_id, attr.value.objlist);
                break;

            default:
                break;
        }
    }
}

void meta_generic_validation_post_get(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_object_id_t switch_id,
        _In_ const uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    switch_id = meta_extract_switch_id(meta_key, switch_id);

    /*
     * TODO We should snoop attributes retrieved from switch and put them to
     * local db if they don't exist since if attr is oid it may lead to
     * inconsistency when counting reference
     */

    for (uint32_t idx = 0; idx < attr_count; ++idx)
    {
        const sai_attribute_t* attr = &attr_list[idx];

        auto mdp = sai_metadata_get_attr_metadata(meta_key.objecttype, attr->id);

        const sai_attribute_value_t& value = attr->value;

        const sai_attr_metadata_t& md = *mdp;

        switch (md.attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_BOOL:
            case SAI_ATTR_VALUE_TYPE_CHARDATA:
            case SAI_ATTR_VALUE_TYPE_UINT8:
            case SAI_ATTR_VALUE_TYPE_INT8:
            case SAI_ATTR_VALUE_TYPE_UINT16:
            case SAI_ATTR_VALUE_TYPE_INT16:
            case SAI_ATTR_VALUE_TYPE_UINT32:
            case SAI_ATTR_VALUE_TYPE_INT32:
            case SAI_ATTR_VALUE_TYPE_UINT64:
            case SAI_ATTR_VALUE_TYPE_INT64:
            case SAI_ATTR_VALUE_TYPE_MAC:
            case SAI_ATTR_VALUE_TYPE_IPV4:
            case SAI_ATTR_VALUE_TYPE_IPV6:
            case SAI_ATTR_VALUE_TYPE_POINTER:
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS:
            case SAI_ATTR_VALUE_TYPE_IP_PREFIX:
                // primitives, ok
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:
                meta_generic_validation_post_get_objlist(meta_key, md, switch_id, 1, &value.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:
                meta_generic_validation_post_get_objlist(meta_key, md, switch_id, value.objlist.count, value.objlist.list);
                break;

            case SAI_ATTR_VALUE_TYPE_VLAN_LIST:

                {
                    uint32_t count = value.vlanlist.count;

                    if (count > MAXIMUM_VLAN_NUMBER)
                    {
                        META_LOG_ERROR(md, "too many vlans returned on vlan list (vendor bug?)");
                    }

                    if (value.vlanlist.list == NULL)
                    {
                        break;
                    }

                    for (uint32_t i = 0; i < count; ++i)
                    {
                        uint16_t vlanid = value.vlanlist.list[i];

                        if (vlanid < MINIMUM_VLAN_NUMBER || vlanid > MAXIMUM_VLAN_NUMBER)
                        {
                            META_LOG_ERROR(md, "vlan id %u is outside range, but returned on list [%u]", vlanid, i);
                        }
                    }

                    break;
                }

                // ACL FIELD

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_IPV6:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:
                if (value.aclfield.enable)
                meta_generic_validation_post_get_objlist(meta_key, md, switch_id, 1, &value.aclfield.data.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:
                if (value.aclfield.enable)
                meta_generic_validation_post_get_objlist(meta_key, md, switch_id, value.aclfield.data.objlist.count, value.aclfield.data.objlist.list);
                break;

                // case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_UINT8_LIST: (2 lists)

                // ACL ACTION

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_BOOL:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT8:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT16:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_UINT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_MAC:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV4:
            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_IPV6:
                // primitives
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:
                if (value.aclaction.enable)
                meta_generic_validation_post_get_objlist(meta_key, md, switch_id, 1, &value.aclaction.parameter.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:
                if (value.aclaction.enable)
                meta_generic_validation_post_get_objlist(meta_key, md, switch_id, value.aclaction.parameter.objlist.count, value.aclaction.parameter.objlist.list);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_CAPABILITY:
                VALIDATION_LIST_GET(md, value.aclcapability.action_list);
                break;

                // ACL END

            case SAI_ATTR_VALUE_TYPE_UINT8_LIST:
                VALIDATION_LIST_GET(md, value.u8list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT8_LIST:
                VALIDATION_LIST_GET(md, value.s8list);
                break;
            case SAI_ATTR_VALUE_TYPE_UINT16_LIST:
                VALIDATION_LIST_GET(md, value.u16list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT16_LIST:
                VALIDATION_LIST_GET(md, value.s16list);
                break;
            case SAI_ATTR_VALUE_TYPE_UINT32_LIST:
                VALIDATION_LIST_GET(md, value.u32list);
                break;
            case SAI_ATTR_VALUE_TYPE_INT32_LIST:
                VALIDATION_LIST_GET(md, value.s32list);
                break;
            case SAI_ATTR_VALUE_TYPE_QOS_MAP_LIST:
                VALIDATION_LIST_GET(md, value.qosmap);
                break;
            case SAI_ATTR_VALUE_TYPE_ACL_RESOURCE_LIST:
                VALIDATION_LIST_GET(md, value.aclresource);
                break;
            case SAI_ATTR_VALUE_TYPE_IP_ADDRESS_LIST:
                VALIDATION_LIST_GET(md, value.ipaddrlist);
                break;

            case SAI_ATTR_VALUE_TYPE_UINT32_RANGE:

                if (value.u32range.min > value.u32range.max)
                {
                    META_LOG_ERROR(md, "invalid range %u .. %u", value.u32range.min, value.u32range.max);
                }

                break;

            case SAI_ATTR_VALUE_TYPE_INT32_RANGE:

                if (value.s32range.min > value.s32range.max)
                {
                    META_LOG_ERROR(md, "invalid range %u .. %u", value.s32range.min, value.s32range.max);
                }

                break;

            default:

                META_LOG_THROW(md, "serialization type is not supported yet FIXME");
        }

        if (md.isenum)
        {
            int32_t val = value.s32;

            switch (md.attrvaluetype)
            {
                case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_INT32:
                    val = value.aclfield.data.s32;
                    break;

                case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_INT32:
                    val = value.aclaction.parameter.s32;
                    break;

                default:
                    val = value.s32;
                    break;
            }

            if (!sai_metadata_is_allowed_enum_value(&md, val))
            {
                META_LOG_ERROR(md, "is enum, but value %d not found on allowed values list", val);
                continue;
            }
        }

        if (md.isenumlist)
        {
            if (value.s32list.list == NULL)
            {
                continue;
            }

            for (uint32_t i = value.s32list.count; i < value.s32list.count; ++i)
            {
                int32_t s32 = value.s32list.list[i];

                if (!sai_metadata_is_allowed_enum_value(&md, s32))
                {
                    META_LOG_ERROR(md, "is enum list, but value %d not found on allowed values list", s32);
                }
            }
        }
    }

    if (meta_key.objecttype == SAI_OBJECT_TYPE_PORT)
    {
        meta_post_port_get(meta_key, switch_id, attr_count, attr_list);
    }
}

// FDB ENTRY

sai_status_t meta_sai_validate_fdb_entry(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ bool create,
        _In_ bool get = false)
{
    SWSS_LOG_ENTER();

    if (fdb_entry == NULL)
    {
        SWSS_LOG_ERROR("fdb_entry pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    //sai_vlan_id_t vlan_id = fdb_entry->vlan_id;

    //if (vlan_id < MINIMUM_VLAN_NUMBER || vlan_id > MAXIMUM_VLAN_NUMBER)
    //{
    //    SWSS_LOG_ERROR("invalid vlan number %d expected <%d..%d>", vlan_id, MINIMUM_VLAN_NUMBER, MAXIMUM_VLAN_NUMBER);

    //    return SAI_STATUS_INVALID_PARAMETER;
    //}

    // check if fdb entry exists

    sai_object_meta_key_t meta_key_fdb = { .objecttype = SAI_OBJECT_TYPE_FDB_ENTRY, .objectkey = { .key = { .fdb_entry = *fdb_entry } } };

    std::string key_fdb = sai_serialize_object_meta_key(meta_key_fdb);

    if (create)
    {
        if (g_saiObjectCollection.objectExists(key_fdb))
        {
            SWSS_LOG_ERROR("object key %s already exists", key_fdb.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }

        return SAI_STATUS_SUCCESS;
    }

    // set, get, remove

    if (!g_saiObjectCollection.objectExists(key_fdb) && !get)
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_fdb.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // fdb entry is valid

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_create_fdb_entry(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_fdb_entry_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(fdb_entry, attr_count, attr_list);

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

sai_status_t meta_sai_remove_fdb_entry(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ sai_remove_fdb_entry_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(fdb_entry);

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

sai_status_t meta_sai_set_fdb_entry(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_fdb_entry_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(fdb_entry, attr);

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

sai_status_t meta_sai_get_fdb_entry(
        _In_ const sai_fdb_entry_t* fdb_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_fdb_entry_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(fdb_entry, attr_count, attr_list);

    META_LOG_STATUS(get, status);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, fdb_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

// MCAST FDB ENTRY

sai_status_t meta_sai_validate_mcast_fdb_entry(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ bool create,
        _In_ bool get = false)
{
    SWSS_LOG_ENTER();

    if (mcast_fdb_entry == NULL)
    {
        SWSS_LOG_ERROR("mcast_fdb_entry pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t bv_id = mcast_fdb_entry->bv_id;

    if (bv_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("bv_id set to null object id");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t object_type = sai_object_type_query(bv_id);

    if (object_type == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_ERROR("bv_id oid 0x%" PRIx64 " is not valid object type, returned null object type", bv_id);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (object_type != SAI_OBJECT_TYPE_BRIDGE && object_type != SAI_OBJECT_TYPE_VLAN)
    {
        SWSS_LOG_ERROR("bv_id oid 0x%" PRIx64 " type %d is wrong type, expected BRIDGE or VLAN", bv_id, object_type);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if bv_id exists

    sai_object_meta_key_t meta_key_bv = { .objecttype = object_type, .objectkey = { .key = { .object_id = bv_id } } };

    std::string key_bv = sai_serialize_object_meta_key(meta_key_bv);

    if (!g_saiObjectCollection.objectExists(key_bv))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_bv.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if fdb entry exists

    sai_object_meta_key_t meta_key_fdb = { .objecttype = SAI_OBJECT_TYPE_MCAST_FDB_ENTRY, .objectkey = { .key = { .mcast_fdb_entry = *mcast_fdb_entry } } };

    std::string key_fdb = sai_serialize_object_meta_key(meta_key_fdb);

    if (create)
    {
        if (g_saiObjectCollection.objectExists(key_fdb))
        {
            SWSS_LOG_ERROR("object key %s already exists", key_fdb.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }

        return SAI_STATUS_SUCCESS;
    }

    // set, get, remove

    if (!g_saiObjectCollection.objectExists(key_fdb) && !get)
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_fdb.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // fdb entry is valid

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_create_mcast_fdb_entry(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_mcast_fdb_entry_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(mcast_fdb_entry, attr_count, attr_list);

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

sai_status_t meta_sai_remove_mcast_fdb_entry(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ sai_remove_mcast_fdb_entry_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(mcast_fdb_entry);

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

sai_status_t meta_sai_set_mcast_fdb_entry(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_mcast_fdb_entry_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(mcast_fdb_entry, attr);

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

sai_status_t meta_sai_get_mcast_fdb_entry(
        _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_mcast_fdb_entry_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(mcast_fdb_entry, attr_count, attr_list);

    META_LOG_STATUS(get, status);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, mcast_fdb_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

// NEIGHBOR ENTRY

sai_status_t meta_sai_validate_neighbor_entry(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ bool create)
{
    SWSS_LOG_ENTER();

    if (neighbor_entry == NULL)
    {
        SWSS_LOG_ERROR("neighbor_entry pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    switch (neighbor_entry->ip_address.addr_family)
    {
        case SAI_IP_ADDR_FAMILY_IPV4:
        case SAI_IP_ADDR_FAMILY_IPV6:
            break;

        default:

            SWSS_LOG_ERROR("invalid address family: %d", neighbor_entry->ip_address.addr_family);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t rif = neighbor_entry->rif_id;

    if (rif == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("router interface is set to null object id");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t object_type = sai_object_type_query(rif);

    if (object_type == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_ERROR("router interface oid 0x%" PRIx64 " is not valid object type, returned null object type", rif);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t expected = SAI_OBJECT_TYPE_ROUTER_INTERFACE;

    if (object_type != expected)
    {
        SWSS_LOG_ERROR("router interface oid 0x%" PRIx64 " type %d is wrong type, expected object type %d", rif, object_type, expected);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if router interface exists

    sai_object_meta_key_t meta_key_rif = { .objecttype = expected, .objectkey = { .key = { .object_id = rif } } };

    std::string key_rif = sai_serialize_object_meta_key(meta_key_rif);

    if (!g_saiObjectCollection.objectExists(key_rif))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_rif.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_meta_key_t meta_key_neighbor = { .objecttype = SAI_OBJECT_TYPE_NEIGHBOR_ENTRY, .objectkey = { .key = { .neighbor_entry = *neighbor_entry } } };

    std::string key_neighbor = sai_serialize_object_meta_key(meta_key_neighbor);

    if (create)
    {
        if (g_saiObjectCollection.objectExists(key_neighbor))
        {
            SWSS_LOG_ERROR("object key %s already exists", key_neighbor.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }

        return SAI_STATUS_SUCCESS;
    }

    // set, get, remove

    if (!g_saiObjectCollection.objectExists(key_neighbor))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_neighbor.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // neighbor entry is valid

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_create_neighbor_entry(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_neighbor_entry_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(neighbor_entry, attr_count, attr_list);

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

sai_status_t meta_sai_remove_neighbor_entry(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ sai_remove_neighbor_entry_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(neighbor_entry);

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

sai_status_t meta_sai_set_neighbor_entry(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_neighbor_entry_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(neighbor_entry, attr);

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

sai_status_t meta_sai_get_neighbor_entry(
        _In_ const sai_neighbor_entry_t* neighbor_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_neighbor_entry_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(neighbor_entry, attr_count, attr_list);

    META_LOG_STATUS(get, status);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, neighbor_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

// ROUTE ENTRY

sai_status_t meta_sai_validate_route_entry(
        _In_ const sai_route_entry_t* route_entry,
        _In_ bool create)
{
    SWSS_LOG_ENTER();

    if (route_entry == NULL)
    {
        SWSS_LOG_ERROR("route_entry pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    auto family = route_entry->destination.addr_family;

    switch (family)
    {
        case SAI_IP_ADDR_FAMILY_IPV4:
            break;

        case SAI_IP_ADDR_FAMILY_IPV6:

            if (!is_ipv6_mask_valid(route_entry->destination.mask.ip6))
            {
                SWSS_LOG_ERROR("invalid ipv6 mask: %s", sai_serialize_ipv6(route_entry->destination.mask.ip6).c_str());

                return SAI_STATUS_INVALID_PARAMETER;
            }

            break;

        default:

            SWSS_LOG_ERROR("invalid prefix family: %d", family);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t vr = route_entry->vr_id;

    if (vr == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("virtual router is set to null object id");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t object_type = sai_object_type_query(vr);

    if (object_type == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_ERROR("virtual router oid 0x%" PRIx64 " is not valid object type, returned null object type", vr);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t expected = SAI_OBJECT_TYPE_VIRTUAL_ROUTER;

    if (object_type != expected)
    {
        SWSS_LOG_ERROR("virtual router oid 0x%" PRIx64 " type %d is wrong type, expected object type %d", vr, object_type, expected);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if virtual router exists

    sai_object_meta_key_t meta_key_vr = { .objecttype = expected, .objectkey = { .key = { .object_id = vr } } };

    std::string key_vr = sai_serialize_object_meta_key(meta_key_vr);

    if (!g_saiObjectCollection.objectExists(key_vr))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_vr.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if route entry exists

    sai_object_meta_key_t meta_key_route = { .objecttype = SAI_OBJECT_TYPE_ROUTE_ENTRY, .objectkey = { .key = { .route_entry = *route_entry } } };

    std::string key_route = sai_serialize_object_meta_key(meta_key_route);

    if (create)
    {
        if (g_saiObjectCollection.objectExists(key_route))
        {
            SWSS_LOG_ERROR("object key %s already exists", key_route.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }

        return SAI_STATUS_SUCCESS;
    }

    // set, get, remove

    if (!g_saiObjectCollection.objectExists(key_route))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_route.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_create_route_entry(
        _In_ const sai_route_entry_t* route_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_route_entry_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(route_entry, attr_count, attr_list);

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

sai_status_t meta_sai_remove_route_entry(
        _In_ const sai_route_entry_t* route_entry,
        _In_ sai_remove_route_entry_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(route_entry);

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

sai_status_t meta_sai_set_route_entry(
        _In_ const sai_route_entry_t* route_entry,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_route_entry_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(route_entry, attr);

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

sai_status_t meta_sai_get_route_entry(
        _In_ const sai_route_entry_t* route_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_route_entry_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(route_entry, attr_count, attr_list);

    META_LOG_STATUS(get, status);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, route_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

// L2MC ENTRY

sai_status_t meta_sai_validate_l2mc_entry(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ bool create)
{
    SWSS_LOG_ENTER();

    if (l2mc_entry == NULL)
    {
        SWSS_LOG_ERROR("l2mc_entry pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    switch (l2mc_entry->type)
    {
        case SAI_L2MC_ENTRY_TYPE_SG:
        case SAI_L2MC_ENTRY_TYPE_XG:
            break;

        default:

            SWSS_LOG_ERROR("invalid l2mc_entry type: %d", l2mc_entry->type);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    auto family = l2mc_entry->destination.addr_family;

    switch (family)
    {
        case SAI_IP_ADDR_FAMILY_IPV4:
        case SAI_IP_ADDR_FAMILY_IPV6:
            break;

        default:

            SWSS_LOG_ERROR("invalid destination family: %d", family);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    family = l2mc_entry->source.addr_family;

    switch (family)
    {
        case SAI_IP_ADDR_FAMILY_IPV4:
        case SAI_IP_ADDR_FAMILY_IPV6:
            break;

        default:

            SWSS_LOG_ERROR("invalid source family: %d", family);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t bv_id = l2mc_entry->bv_id;

    if (bv_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("bv_id set to null object id");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t object_type = sai_object_type_query(bv_id);

    if (object_type == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_ERROR("bv_id oid 0x%" PRIx64 " is not valid object type, returned null object type", bv_id);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (object_type != SAI_OBJECT_TYPE_BRIDGE && object_type != SAI_OBJECT_TYPE_VLAN)
    {
        SWSS_LOG_ERROR("bv_id oid 0x%" PRIx64 " type %d is wrong type, expected BRIDGE or VLAN", bv_id, object_type);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if bv_id exists

    sai_object_meta_key_t meta_key_bv = { .objecttype = object_type, .objectkey = { .key = { .object_id = bv_id } } };

    std::string key_bv = sai_serialize_object_meta_key(meta_key_bv);

    if (!g_saiObjectCollection.objectExists(key_bv))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_bv.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if l2mc entry exists

    sai_object_meta_key_t meta_key_route = { .objecttype = SAI_OBJECT_TYPE_L2MC_ENTRY, .objectkey = { .key = { .l2mc_entry = *l2mc_entry } } };

    std::string key_route = sai_serialize_object_meta_key(meta_key_route);

    if (create)
    {
        if (g_saiObjectCollection.objectExists(key_route))
        {
            SWSS_LOG_ERROR("object key %s already exists", key_route.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }

        return SAI_STATUS_SUCCESS;
    }

    // set, get, remove

    if (!g_saiObjectCollection.objectExists(key_route))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_route.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_create_l2mc_entry(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_l2mc_entry_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(l2mc_entry, attr_count, attr_list);

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

sai_status_t meta_sai_remove_l2mc_entry(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ sai_remove_l2mc_entry_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(l2mc_entry);

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

sai_status_t meta_sai_set_l2mc_entry(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_l2mc_entry_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(l2mc_entry, attr);

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

sai_status_t meta_sai_get_l2mc_entry(
        _In_ const sai_l2mc_entry_t* l2mc_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_l2mc_entry_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(l2mc_entry, attr_count, attr_list);

    META_LOG_STATUS(get, status);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, l2mc_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

// IPMC ENTRY

sai_status_t meta_sai_validate_ipmc_entry(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ bool create)
{
    SWSS_LOG_ENTER();

    if (ipmc_entry == NULL)
    {
        SWSS_LOG_ERROR("ipmc_entry pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    switch (ipmc_entry->type)
    {
        case SAI_IPMC_ENTRY_TYPE_SG:
        case SAI_IPMC_ENTRY_TYPE_XG:
            break;

        default:

            SWSS_LOG_ERROR("invalid ipmc_entry type: %d", ipmc_entry->type);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    auto family = ipmc_entry->destination.addr_family;

    switch (family)
    {
        case SAI_IP_ADDR_FAMILY_IPV4:
        case SAI_IP_ADDR_FAMILY_IPV6:
            break;

        default:

            SWSS_LOG_ERROR("invalid destination family: %d", family);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    family = ipmc_entry->source.addr_family;

    switch (family)
    {
        case SAI_IP_ADDR_FAMILY_IPV4:
        case SAI_IP_ADDR_FAMILY_IPV6:
            break;

        default:

            SWSS_LOG_ERROR("invalid source family: %d", family);

            return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t vr_id = ipmc_entry->vr_id;

    if (vr_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("vr_id set to null object id");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t object_type = sai_object_type_query(vr_id);

    if (object_type == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_ERROR("vr_id oid 0x%" PRIx64 " is not valid object type, returned null object type", vr_id);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (object_type != SAI_OBJECT_TYPE_VIRTUAL_ROUTER)
    {
        SWSS_LOG_ERROR("vr_id oid 0x%" PRIx64 " type %d is wrong type, expected VIRTUAL_ROUTER", vr_id, object_type);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if vr_id exists

    sai_object_meta_key_t meta_key_bv = { .objecttype = object_type, .objectkey = { .key = { .object_id = vr_id } } };

    std::string key_bv = sai_serialize_object_meta_key(meta_key_bv);

    if (!g_saiObjectCollection.objectExists(key_bv))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_bv.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if ipmc entry exists

    sai_object_meta_key_t meta_key_route = { .objecttype = SAI_OBJECT_TYPE_IPMC_ENTRY, .objectkey = { .key = { .ipmc_entry = *ipmc_entry } } };

    std::string key_route = sai_serialize_object_meta_key(meta_key_route);

    if (create)
    {
        if (g_saiObjectCollection.objectExists(key_route))
        {
            SWSS_LOG_ERROR("object key %s already exists", key_route.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }

        return SAI_STATUS_SUCCESS;
    }

    // set, get, remove

    if (!g_saiObjectCollection.objectExists(key_route))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_route.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_create_ipmc_entry(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_ipmc_entry_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(ipmc_entry, attr_count, attr_list);

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

sai_status_t meta_sai_remove_ipmc_entry(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ sai_remove_ipmc_entry_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(ipmc_entry);

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

sai_status_t meta_sai_set_ipmc_entry(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_ipmc_entry_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(ipmc_entry, attr);

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

sai_status_t meta_sai_get_ipmc_entry(
        _In_ const sai_ipmc_entry_t* ipmc_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_ipmc_entry_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(ipmc_entry, attr_count, attr_list);

    META_LOG_STATUS(get, status);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, ipmc_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

// INSEG ENTRY

sai_status_t meta_sai_validate_inseg_entry(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ bool create)
{
    SWSS_LOG_ENTER();

    if (inseg_entry == NULL)
    {
        SWSS_LOG_ERROR("inseg_entry pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // validate mpls label

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_create_inseg_entry(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_inseg_entry_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(inseg_entry, attr_count, attr_list);

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

sai_status_t meta_sai_remove_inseg_entry(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ sai_remove_inseg_entry_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(inseg_entry);

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

sai_status_t meta_sai_set_inseg_entry(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_inseg_entry_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(inseg_entry, attr);

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

sai_status_t meta_sai_get_inseg_entry(
        _In_ const sai_inseg_entry_t* inseg_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_inseg_entry_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(inseg_entry, attr_count, attr_list);

    META_LOG_STATUS(get, status);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_generic_validation_post_get(meta_key, inseg_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

// GENERIC

sai_status_t meta_sai_validate_oid(
        _In_ sai_object_type_t object_type,
        _In_ const sai_object_id_t* object_id,
        _In_ sai_object_id_t switch_id,
        _In_ bool create)
{
    SWSS_LOG_ENTER();

    if (object_type <= SAI_OBJECT_TYPE_NULL ||
            object_type >= SAI_OBJECT_TYPE_EXTENSIONS_MAX)
    {
        SWSS_LOG_ERROR("invalid object type specified: %d, FIXME", object_type);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    const char* otname =  sai_metadata_get_enum_value_name(&sai_metadata_enum_sai_object_type_t, object_type);

    auto info = sai_metadata_get_object_type_info(object_type);

    if (info->isnonobjectid)
    {
        SWSS_LOG_THROW("invalid object type (%s) specified as generic, FIXME", otname);
    }

    SWSS_LOG_DEBUG("generic object type: %s", otname);

    if (object_id == NULL)
    {
        SWSS_LOG_ERROR("oid pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (create)
    {
        return SAI_STATUS_SUCCESS;
    }

    sai_object_id_t oid = *object_id;

    if (oid == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("oid is set to null object id on %s", otname);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t ot = sai_object_type_query(oid);

    if (ot == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_ERROR("%s oid 0x%" PRIx64 " is not valid object type, returned null object type", otname, oid);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t expected = object_type;

    if (ot != expected)
    {
        SWSS_LOG_ERROR("%s oid 0x%" PRIx64 " type %d is wrong type, expected object type %d", otname, oid, ot, expected);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if object exists

    sai_object_meta_key_t meta_key_oid = { .objecttype = expected, .objectkey = { .key = { .object_id = oid } } };

    std::string key_oid = sai_serialize_object_meta_key(meta_key_oid);

    if (!g_saiObjectCollection.objectExists(key_oid))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_oid.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_create_oid(
        _In_ sai_object_type_t object_type,
        _Out_ sai_object_id_t* object_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_generic_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(object_type, object_id, switch_id, attr_count, attr_list);

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

sai_status_t meta_sai_remove_oid(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ sai_remove_generic_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(object_type, object_id);

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

sai_status_t meta_sai_set_oid(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_generic_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(object_type, object_id, attr);

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

sai_status_t meta_sai_get_oid(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_generic_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(object_type, object_id, attr_count, attr_list);

    META_LOG_STATUS(get, status);

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

// STATS

sai_status_t meta_generic_validation_get_stats(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ const sai_enum_metadata_t* stats_enum,
        _In_ uint32_t count,
        _In_ const int32_t *counter_id_list,
        _In_ const uint64_t *counter_list)
{
    SWSS_LOG_ENTER();

    if (meta_unittests_enabled() && (count & 0x80000000))
    {
        /*
         * If last bit of counters count is set to high, and unittests are enabled,
         * then this api can be used to SET counter values by user for debugging purposes.
         */
        count = count & ~0x80000000;
    }

    if (count < 1)
    {
        SWSS_LOG_ERROR("expected at least 1 stat when calling get_stats, zero given");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (count > MAX_LIST_COUNT)
    {
        SWSS_LOG_ERROR("get stats count %u > max list count %u", count, MAX_LIST_COUNT);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (counter_id_list == NULL)
    {
        SWSS_LOG_ERROR("counter id list pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (counter_list == NULL)
    {
        SWSS_LOG_ERROR("counter list pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (stats_enum == NULL)
    {
        SWSS_LOG_ERROR("enum metadata pointer is NULL, bug?");

        return SAI_STATUS_FAILURE;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        if (sai_metadata_get_enum_value_name(stats_enum, counter_id_list[i]) == NULL)
        {
            SWSS_LOG_ERROR("counter id %u is not allowed on %s", counter_id_list[i], stats_enum->name);

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_get_stats_oid(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_enum_metadata_t* stats_enum,
        _In_ uint32_t count,
        _In_ const int32_t *counter_id_list,
        _Out_ uint64_t *counter_list,
        _In_ sai_get_generic_stats_fn get_stats)
{
    SWSS_LOG_ENTER();

    sai_object_id_t switch_id = sai_switch_id_query(object_id);

    sai_status_t status = meta_sai_validate_oid(object_type, &object_id, switch_id, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = object_type, .objectkey = { .key = { .object_id  = object_id } } };

    status = meta_generic_validation_get_stats(meta_key, stats_enum, count, counter_id_list, counter_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        return status;
    }

    if (get_stats == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    status = get_stats(object_type, object_id, stats_enum, count, counter_id_list, counter_list);

    META_LOG_STATUS(get, status);

    return status;
}

sai_status_t meta_generic_validation_clear_stats(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ const sai_enum_metadata_t* stats_enum,
        _In_ uint32_t count,
        _In_ const int32_t *counter_id_list)
{
    SWSS_LOG_ENTER();

    if (meta_unittests_enabled() && (count & 0x80000000))
    {
        /*
         * If last bit of counters count is set to high, and unittests are enabled,
         * then this api can be used to SET counter values by user for debugging purposes.
         */
        count = count & ~0x80000000;
    }

    if (count < 1)
    {
        SWSS_LOG_ERROR("expected at least 1 stat when calling clear_stats, zero given");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (count > MAX_LIST_COUNT)
    {
        SWSS_LOG_ERROR("clear_stats count %u > max list count %u", count, MAX_LIST_COUNT);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (counter_id_list == NULL)
    {
        SWSS_LOG_ERROR("counter id list pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (stats_enum == NULL)
    {
        SWSS_LOG_ERROR("enum metadata pointer is NULL, bug?");

        return SAI_STATUS_FAILURE;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        if (sai_metadata_get_enum_value_name(stats_enum, counter_id_list[i]) == NULL)
        {
            SWSS_LOG_ERROR("counter id %u is not allowed on %s", counter_id_list[i], stats_enum->name);

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t meta_sai_clear_stats_oid(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const sai_enum_metadata_t* stats_enum,
        _In_ uint32_t count,
        _In_ const int32_t *counter_id_list,
        _In_ sai_clear_generic_stats_fn clear_stats)
{
    SWSS_LOG_ENTER();

    sai_object_id_t switch_id = sai_switch_id_query(object_id);

    sai_status_t status = meta_sai_validate_oid(object_type, &object_id, switch_id, false);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("oid validation failed");
        return status;
    }

    sai_object_meta_key_t meta_key = { .objecttype = object_type, .objectkey = { .key = { .object_id  = object_id } } };

    // This ensures that all counter ids in counter id list are valid
    // with regards to stats_enum before calling clear_stats()
    status = meta_generic_validation_clear_stats(meta_key, stats_enum, count, counter_id_list);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("clear_stats generic validation failed");
        return status;
    }

    if (clear_stats == NULL)
    {
        SWSS_LOG_ERROR("clear_stats function pointer is NULL");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    status = clear_stats(object_type, object_id, stats_enum, count, counter_id_list);

    META_LOG_STATUS(clear, status);

    return status;
}

// NOTIFICATIONS

static sai_mac_t zero_mac = { 0, 0, 0, 0, 0, 0 };

void meta_sai_on_fdb_flush_event_consolidated(
        _In_ const sai_fdb_event_notification_data_t& data)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_TIMER("fdb flush");

    // since we don't keep objects by type, we need to scan via all objects
    // and find fdb entries

    // TODO on flush we need to respect switch id, and remove fdb entries only
    // from selected switch when adding multiple switch support

    auto bpid = sai_metadata_get_attr_by_id(SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID, data.attr_count, data.attr);
    auto type = sai_metadata_get_attr_by_id(SAI_FDB_ENTRY_ATTR_TYPE, data.attr_count, data.attr);

    if (type == NULL)
    {
        SWSS_LOG_ERROR("FATAL: fdb flush notification don't contain SAI_FDB_ENTRY_ATTR_TYPE attribute! bug!, no entries were flushed in local DB!");
        return;
    }

    SWSS_LOG_NOTICE("processing consolidated fdb flush event of type: %s",
            sai_metadata_get_fdb_entry_type_name((sai_fdb_entry_type_t)type->value.s32));

    std::vector<sai_object_meta_key_t> toremove;

    auto fdbEntries = g_saiObjectCollection.getObjectsByObjectType(SAI_OBJECT_TYPE_FDB_ENTRY);

    for (auto& fdb: fdbEntries)
    {
        auto fdbTypeAttr = fdb->getAttr(SAI_FDB_ENTRY_ATTR_TYPE);

        if (!fdbTypeAttr)
        {
            SWSS_LOG_ERROR("FATAL: missing SAI_FDB_ENTRY_ATTR_TYPE on %s! bug! skipping flush", fdb->getStrMetaKey().c_str());
            continue;
        }

        if (fdbTypeAttr->getSaiAttr()->value.s32 != type->value.s32)
        {
            // entry type is not matching on this fdb entry
            continue;
        }

        // only consider bridge port id if it's defined and value is not NULL
        // since vendor can add this attribute to fdb_entry with NULL value
        if (bpid != NULL && bpid->value.oid != SAI_NULL_OBJECT_ID)
        {
            auto bpidAttr = fdb->getAttr(SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID);

            if (!bpidAttr)
            {
                // port is not defined for this fdb entry
                continue;
            }

            if (bpidAttr->getSaiAttr()->value.oid != bpid->value.oid)
            {
                // bridge port is not matching this fdb entry
                continue;
            }
        }

        auto& meta_key_fdb = fdb->getMetaKey();

        if (data.fdb_entry.bv_id != SAI_NULL_OBJECT_ID)
        {
            if (data.fdb_entry.bv_id != meta_key_fdb.objectkey.key.fdb_entry.bv_id)
            {
                // vlan/bridge id is not matching on this fdb entry
                continue;
            }
        }

        // this fdb entry is matching, removing

        SWSS_LOG_INFO("removing %s", fdb->getStrMetaKey().c_str());

        // since meta_generic_validation_post_remove also modifies g_saiObjectCollection
        // we need to push this to a vector and remove in next loop
        toremove.push_back(meta_key_fdb);
    }

    for (auto it = toremove.begin(); it != toremove.end(); ++it)
    {
        // remove selected objects
        meta_generic_validation_post_remove(*it);
    }
}

void meta_fdb_event_snoop_oid(
        _In_ sai_object_id_t oid)
{
    SWSS_LOG_ENTER();

    if (oid == SAI_NULL_OBJECT_ID)
        return;

    if (g_oids.objectReferenceExists(oid))
        return;

    sai_object_type_t ot = sai_object_type_query(oid);

    if (ot == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_ERROR("failed to get object type on fdb_event oid: 0x%" PRIx64 "", oid);
        return;
    }

    sai_object_meta_key_t key = { .objecttype = ot, .objectkey = { .key = { .object_id = oid } } };

    g_oids.objectReferenceInsert(oid);

    if (!g_saiObjectCollection.objectExists(key))
        g_saiObjectCollection.createObject(key);

    /*
     * In normal operation orch agent should query or create all bridge, vlan
     * and bridge port, so we should not get this message. Let's put it as
     * warning for better visibility. Most likely if this happen  there is a
     * vendor bug in SAI and we should also see warnings or errors reported
     * from syncd in logs.
     */

    SWSS_LOG_WARN("fdb_entry oid (snoop): %s: %s",
            sai_serialize_object_type(ot).c_str(),
            sai_serialize_object_id(oid).c_str());
}

void meta_sai_on_fdb_event_single(
        _In_ const sai_fdb_event_notification_data_t& data)
{
    SWSS_LOG_ENTER();

    const sai_object_meta_key_t meta_key_fdb = { .objecttype = SAI_OBJECT_TYPE_FDB_ENTRY, .objectkey = { .key = { .fdb_entry = data.fdb_entry } } };

    std::string key_fdb = sai_serialize_object_meta_key(meta_key_fdb);

    /*
     * Because we could receive fdb event's before orch agent will query or
     * create bridge/vlan/bridge port we should snoop here new OIDs and put
     * them in local DB.
     *
     * Unfortunately we don't have a way to check whether those OIDs are correct
     * or whether there maybe some bug in vendor SAI and for example is sending
     * invalid OIDs in those event's. Also sai_object_type_query can return
     * valid object type for OID, but this does not guarantee that this OID is
     * valid, for example one of existing bridge ports that orch agent didn't
     * query yet.
     */

    meta_fdb_event_snoop_oid(data.fdb_entry.bv_id);

    for (uint32_t i = 0; i < data.attr_count; i++)
    {
        auto meta = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_FDB_ENTRY, data.attr[i].id);

        if (meta == NULL)
        {
            SWSS_LOG_ERROR("failed to get metadata for fdb_entry attr.id = %d", data.attr[i].id);
            continue;
        }

        if (meta->attrvaluetype == SAI_ATTR_VALUE_TYPE_OBJECT_ID)
            meta_fdb_event_snoop_oid(data.attr[i].value.oid);
    }

    switch (data.event_type)
    {
        case SAI_FDB_EVENT_LEARNED:

            if (g_saiObjectCollection.objectExists(key_fdb))
            {
                SWSS_LOG_WARN("object key %s alearedy exists, but received LEARNED event", key_fdb.c_str());
                break;
            }

            {
                sai_attribute_t *list = data.attr;
                uint32_t count = data.attr_count;

                sai_attribute_t local[2]; // 2 for port id and type

                if (count == 1)
                {
                    // workaround for missing "TYPE" attribute on notification

                    local[0] = data.attr[0]; // copy 1st attr
                    local[1].id = SAI_FDB_ENTRY_ATTR_TYPE;
                    local[1].value.s32 = SAI_FDB_ENTRY_TYPE_DYNAMIC; // assume learned entries are always dynamic

                    list = local;
                    count = 2; // now we added type
                }

                sai_status_t status = meta_generic_validation_create(meta_key_fdb, data.fdb_entry.switch_id, count, list);

                if (status == SAI_STATUS_SUCCESS)
                {
                    meta_generic_validation_post_create(meta_key_fdb, data.fdb_entry.switch_id, count, list);
                }
                else
                {
                    SWSS_LOG_ERROR("failed to insert %s received in notification: %s", key_fdb.c_str(), sai_serialize_status(status).c_str());
                }
            }

            break;

        case SAI_FDB_EVENT_AGED:

            if (!g_saiObjectCollection.objectExists(key_fdb))
            {
                SWSS_LOG_WARN("object key %s doesn't exist but received AGED event", key_fdb.c_str());
                break;
            }

            meta_generic_validation_post_remove(meta_key_fdb);

            break;

        case SAI_FDB_EVENT_FLUSHED:

            if (memcmp(data.fdb_entry.mac_address, zero_mac, sizeof(zero_mac)) == 0)
            {
                meta_sai_on_fdb_flush_event_consolidated(data);
                break;
            }

            if (!g_saiObjectCollection.objectExists(key_fdb))
            {
                SWSS_LOG_WARN("object key %s doesn't exist but received FLUSHED event", key_fdb.c_str());
                break;
            }

            meta_generic_validation_post_remove(meta_key_fdb);

            break;

        case SAI_FDB_EVENT_MOVE:

            if (!g_saiObjectCollection.objectExists(key_fdb))
            {
                SWSS_LOG_WARN("object key %s doesn't exist but received FDB MOVE event", key_fdb.c_str());
                break;
            }

            // on MOVE event, just update attributes on existing entry

            for (uint32_t i = 0; i < data.attr_count; i++)
            {
                const sai_attribute_t& attr = data.attr[i];

                sai_status_t status = meta_generic_validation_set(meta_key_fdb, &attr);

                if (status != SAI_STATUS_SUCCESS)
                {
                    SWSS_LOG_ERROR("object key %s FDB MOVE event, SET validateion failed on attr.id = %d", key_fdb.c_str(), attr.id);
                    continue;
                }

                meta_generic_validation_post_set(meta_key_fdb, &attr);
            }

            break;

        default:

            SWSS_LOG_ERROR("got FDB_ENTRY notification with unknown event_type %d, bug?", data.event_type);
            break;
    }
}

void meta_sai_on_fdb_event(
        _In_ uint32_t count,
        _In_ sai_fdb_event_notification_data_t *data)
{
    SWSS_LOG_ENTER();

    if (count && data == NULL)
    {
        SWSS_LOG_ERROR("fdb_event_notification_data pointer is NULL when count is %u", count);
        return;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        meta_sai_on_fdb_event_single(data[i]);
    }
}

void meta_sai_on_switch_state_change(
        _In_ sai_object_id_t switch_id,
        _In_ sai_switch_oper_status_t switch_oper_status)
{
    SWSS_LOG_ENTER();

    auto ot = sai_object_type_query(switch_id);

    if (ot != SAI_OBJECT_TYPE_SWITCH)
    {
        SWSS_LOG_WARN("switch_id %s is of type %s, but expected SAI_OBJECT_TYPE_SWITCH",
                sai_serialize_object_id(switch_id).c_str(),
                sai_serialize_object_type(ot).c_str());
    }

    sai_object_meta_key_t switch_meta_key = { .objecttype = ot , .objectkey = { .key = { .object_id = switch_id } } };

    if (!g_saiObjectCollection.objectExists(switch_meta_key))
    {
        SWSS_LOG_ERROR("switch_id %s don't exists in local database",
                sai_serialize_object_id(switch_id).c_str());
    }

    // we should not snoop switch_id, since switch id should be created directly by user

    if (!sai_metadata_get_enum_value_name(
                &sai_metadata_enum_sai_switch_oper_status_t,
                switch_oper_status))
    {
        SWSS_LOG_WARN("switch oper status value (%d) not found in sai_switch_oper_status_t",
                switch_oper_status);
    }
}

void meta_sai_on_switch_shutdown_request(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    auto ot = sai_object_type_query(switch_id);

    if (ot != SAI_OBJECT_TYPE_SWITCH)
    {
        SWSS_LOG_WARN("switch_id %s is of type %s, but expected SAI_OBJECT_TYPE_SWITCH",
                sai_serialize_object_id(switch_id).c_str(),
                sai_serialize_object_type(ot).c_str());
    }

    sai_object_meta_key_t switch_meta_key = { .objecttype = ot , .objectkey = { .key = { .object_id = switch_id } } };

    if (!g_saiObjectCollection.objectExists(switch_meta_key))
    {
        SWSS_LOG_ERROR("switch_id %s don't exists in local database",
                sai_serialize_object_id(switch_id).c_str());
    }

    // we should not snoop switch_id, since switch id should be created directly by user
}

static void meta_sai_on_port_state_change_single(
        _In_ const sai_port_oper_status_notification_t& data)
{
    SWSS_LOG_ENTER();

    auto ot = sai_object_type_query(data.port_id);

    bool valid = false;

    switch (ot)
    {
        // TODO hardcoded types, must advance SAI repository commit to get metadata for this
        case SAI_OBJECT_TYPE_PORT:
        case SAI_OBJECT_TYPE_BRIDGE_PORT:
        case SAI_OBJECT_TYPE_LAG:

            valid = true;
            break;

        default:

            SWSS_LOG_ERROR("data.port_id %s has unexpected type: %s, expected PORT, BRIDGE_PORT or LAG",
                    sai_serialize_object_id(data.port_id).c_str(),
                    sai_serialize_object_type(ot).c_str());
            break;
    }

    SWSS_LOG_WARN("data.port_id has invalid type, skip snoop");

    if (valid && !g_oids.objectReferenceExists(data.port_id))
    {
        SWSS_LOG_NOTICE("data.port_id new object spotted %s not present in local DB (snoop!)",
                sai_serialize_object_id(data.port_id).c_str());

        sai_object_meta_key_t key = { .objecttype = ot, .objectkey = { .key = { .object_id = data.port_id } } };

        g_oids.objectReferenceInsert(data.port_id);

        if (!g_saiObjectCollection.objectExists(key))
        {
            g_saiObjectCollection.createObject(key);
        }
    }

    if (!sai_metadata_get_enum_value_name(
                &sai_metadata_enum_sai_port_oper_status_t,
                data.port_state))
    {
        SWSS_LOG_WARN("port_state value (%d) not found in sai_port_oper_status_t",
                data.port_state);
    }
}

void meta_sai_on_port_state_change(
        _In_ uint32_t count,
        _In_ const sai_port_oper_status_notification_t *data)
{
    SWSS_LOG_ENTER();

    if (count && data == NULL)
    {
        SWSS_LOG_ERROR("port_oper_status_notification pointer is NULL but count is %u", count);
        return;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        meta_sai_on_port_state_change_single(data[i]);
    }
}

static void meta_sai_on_queue_pfc_deadlock_notification_single(
        _In_ const sai_queue_deadlock_notification_data_t& data)
{
    SWSS_LOG_ENTER();

    auto ot = sai_object_type_query(data.queue_id);

    bool valid = false;

    switch (ot)
    {
        // TODO hardcoded types, must advance SAI repository commit to get metadata for this
        case SAI_OBJECT_TYPE_QUEUE:

            valid = true;
            break;

        default:

            SWSS_LOG_ERROR("data.queue_id %s has unexpected type: %s, expected PORT, BRIDGE_PORT or LAG",
                    sai_serialize_object_id(data.queue_id).c_str(),
                    sai_serialize_object_type(ot).c_str());
            break;
    }

    SWSS_LOG_WARN("data.queue_id has invalid type, skip snoop");

    if (valid && !g_oids.objectReferenceExists(data.queue_id))
    {
        SWSS_LOG_NOTICE("data.queue_id new object spotted %s not present in local DB (snoop!)",
                sai_serialize_object_id(data.queue_id).c_str());

        sai_object_meta_key_t key = { .objecttype = ot, .objectkey = { .key = { .object_id = data.queue_id } } };

        g_oids.objectReferenceInsert(data.queue_id);

        if (!g_saiObjectCollection.objectExists(key))
        {
            g_saiObjectCollection.createObject(key);
        }
    }
}

void meta_sai_on_queue_pfc_deadlock_notification(
        _In_ uint32_t count,
        _In_ const sai_queue_deadlock_notification_data_t *data)
{
    SWSS_LOG_ENTER();

    if (count && data == NULL)
    {
        SWSS_LOG_ERROR("queue_deadlock_notification_data pointer is NULL but count is %u", count);
        return;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        meta_sai_on_queue_pfc_deadlock_notification_single(data[i]);
    }
}

// FDB FLUSH

sai_status_t meta_sai_flush_fdb_entries(
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_flush_fdb_entries_fn flush_fdb_entries)
{
    SWSS_LOG_ENTER();

    if (flush_fdb_entries == NULL)
    {
        SWSS_LOG_ERROR("function pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (attr_count > MAX_LIST_COUNT)
    {
        SWSS_LOG_ERROR("create attribute count %u > max list count %u", attr_count, MAX_LIST_COUNT);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (attr_count != 0 && attr_list == NULL)
    {
        SWSS_LOG_ERROR("attribute list is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t swot = sai_object_type_query(switch_id);

    if (swot != SAI_OBJECT_TYPE_SWITCH)
    {
        SWSS_LOG_ERROR("object type for switch_id %s is %s",
                sai_serialize_object_id(switch_id).c_str(),
                sai_serialize_object_type(swot).c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (!g_oids.objectReferenceExists(switch_id))
    {
        SWSS_LOG_ERROR("switch id %s doesn't exist",
                sai_serialize_object_id(switch_id).c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // validate attributes
    // - attribute list can be empty
    // - validation is similar to "create" action but there is no
    //   post create step and no references are updated
    // - fdb entries are updated in fdb notification

    std::unordered_map<sai_attr_id_t, const sai_attribute_t*> attrs;

    SWSS_LOG_DEBUG("attr count = %u", attr_count);

    for (uint32_t idx = 0; idx < attr_count; ++idx)
    {
        const sai_attribute_t* attr = &attr_list[idx];

        auto mdp = sai_metadata_get_attr_metadata(SAI_OBJECT_TYPE_FDB_FLUSH, attr->id);

        if (mdp == NULL)
        {
            SWSS_LOG_ERROR("unable to find attribute metadata SAI_OBJECT_TYPE_FDB_FLUSH:%d", attr->id);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        const sai_attribute_value_t& value = attr->value;

        const sai_attr_metadata_t& md = *mdp;

        META_LOG_DEBUG(md, "(fdbflush)");

        if (attrs.find(attr->id) != attrs.end())
        {
            META_LOG_ERROR(md, "attribute id (%u) is defined on attr list multiple times", attr->id);

            return SAI_STATUS_INVALID_PARAMETER;
        }

        attrs[attr->id] = attr;

        if (md.flags != SAI_ATTR_FLAGS_CREATE_ONLY)
        {
            META_LOG_ERROR(md, "attr is expected to be marked as CREATE_ONLY");

            return SAI_STATUS_INVALID_PARAMETER;
        }

        if (md.isconditional || md.validonlylength > 0)
        {
            META_LOG_ERROR(md, "attr should not be conditional or validonly");

            return SAI_STATUS_INVALID_PARAMETER;
        }

        switch (md.attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_UINT16:

                if (md.isvlan && (value.u16 >= 0xFFF || value.u16 == 0))
                {
                    META_LOG_ERROR(md, "is vlan id but has invalid id %u", value.u16);

                    return SAI_STATUS_INVALID_PARAMETER;
                }

                break;

            case SAI_ATTR_VALUE_TYPE_INT32:

                if (md.isenum && !sai_metadata_is_allowed_enum_value(&md, value.s32))
                {
                    META_LOG_ERROR(md, "is enum, but value %d not found on allowed values list", value.s32);

                    return SAI_STATUS_INVALID_PARAMETER;
                }

                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:

                {
                    sai_status_t status = meta_generic_validation_objlist(md, switch_id, 1, &value.oid);

                    if (status != SAI_STATUS_SUCCESS)
                    {
                        return status;
                    }

                    break;
                }

            default:

                META_LOG_THROW(md, "serialization type is not supported yet FIXME");
        }
    }

    // there are no mandatory attributes
    // there are no conditional attributes

    return flush_fdb_entries(switch_id, attr_count, attr_list);
}

// NAT

sai_status_t meta_sai_validate_nat_entry(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ bool create)
{
    SWSS_LOG_ENTER();

    if (nat_entry == NULL)
    {
        SWSS_LOG_ERROR("nat_entry pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t vr = nat_entry->vr_id;

    if (vr == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("virtual router is set to null object id");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t object_type = sai_object_type_query(vr);

    if (object_type == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_ERROR("virtual router oid 0x%lx is not valid object type, "
                        "returned null object type", vr);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t expected = SAI_OBJECT_TYPE_VIRTUAL_ROUTER;

    if (object_type != expected)
    {
        SWSS_LOG_ERROR("virtual router oid 0x%lx type %d is wrong type, "
                       "expected object type %d", vr, object_type, expected);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if virtual router exists
    sai_object_meta_key_t meta_key_vr = { .objecttype = expected, .objectkey = { .key = { .object_id = vr } } };

    std::string key_vr = sai_serialize_object_meta_key(meta_key_vr);

    if (!g_saiObjectCollection.objectExists(key_vr))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_vr.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if NAT entry exists
    sai_object_meta_key_t meta_key_nat = { .objecttype = SAI_OBJECT_TYPE_NAT_ENTRY, .objectkey = { .key = { .nat_entry = *nat_entry } } };

    std::string key_nat = sai_serialize_object_meta_key(meta_key_nat);

    if (create)
    {
        if (g_saiObjectCollection.objectExists(key_nat))
        {
            SWSS_LOG_ERROR("object key %s already exists", key_nat.c_str());

            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }

        return SAI_STATUS_SUCCESS;
    }

    // set, get, remove
    if (!g_saiObjectCollection.objectExists(key_nat))
    {
        SWSS_LOG_ERROR("object key %s doesn't exist", key_nat.c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    return SAI_STATUS_SUCCESS;
}


sai_status_t meta_sai_create_nat_entry(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _In_ sai_create_nat_entry_fn create)
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

    if (create == NULL)
    {
        SWSS_LOG_ERROR("create function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = create(nat_entry, attr_count, attr_list);

    META_LOG_STATUS(create, status);

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

sai_status_t meta_sai_remove_nat_entry(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ sai_remove_nat_entry_fn remove)
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

    if (remove == NULL)
    {
        SWSS_LOG_ERROR("remove function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = remove(nat_entry);

    META_LOG_STATUS(remove, status);

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

sai_status_t meta_sai_set_nat_entry(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ const sai_attribute_t *attr,
        _In_ sai_set_nat_entry_attribute_fn set)
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

    if (set == NULL)
    {
        SWSS_LOG_ERROR("set function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = set(nat_entry, attr);

    META_LOG_STATUS(set, status);

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

sai_status_t meta_sai_get_nat_entry(
        _In_ const sai_nat_entry_t* nat_entry,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list,
        _In_ sai_get_nat_entry_attribute_fn get)
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

    if (get == NULL)
    {
        SWSS_LOG_ERROR("get function pointer is NULL");

        return SAI_STATUS_FAILURE;
    }

    status = get(nat_entry, attr_count, attr_list);

    META_LOG_STATUS(get, status);

    if (status == SAI_STATUS_SUCCESS)
    {
      meta_generic_validation_post_get(meta_key, nat_entry->switch_id, attr_count, attr_list);
    }

    return status;
}

static sai_status_t meta_validate_sai_query_attribute_enum_values_capability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ sai_attr_id_t attr_id,
        _Inout_ sai_s32_list_t *enum_values_capability)
{
    SWSS_LOG_ENTER();

    // check if switch_exists
    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_SWITCH, .objectkey = { .key = { .object_id = switch_id } } };

    if (!g_saiObjectCollection.objectExists(meta_key))
    {
        SWSS_LOG_ERROR("switch %s don't exists",
                sai_serialize_object_id(switch_id).c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if switch has correct object type
    sai_object_type_t ot = sai_object_type_query(switch_id);

    if (ot != SAI_OBJECT_TYPE_SWITCH)
    {
        SWSS_LOG_ERROR("switch_id %s must be object type SWITCH, but is %s",
                sai_serialize_object_id(switch_id).c_str(),
                sai_serialize_object_type(ot).c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check specified object type
    if (object_type <= SAI_OBJECT_TYPE_NULL ||
            object_type >= SAI_OBJECT_TYPE_EXTENSIONS_MAX)
    {
        SWSS_LOG_ERROR("invalid object type specified: %d, FIXME", object_type);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if specified attribute is valid

    auto *md = sai_metadata_get_attr_metadata(object_type, attr_id);

    if (md == NULL)
    {
        SWSS_LOG_ERROR("failed to get metadata for object type %s and attr id %d",
                sai_serialize_object_type(object_type).c_str(),
                attr_id);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if attribute is enum or enum list

    if (!( md->isenum || md->isenumlist))
    {
        SWSS_LOG_ERROR("%s is not enum/enumlist", md->attridname);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check capability pointer
    if (enum_values_capability == NULL)
    {
        SWSS_LOG_ERROR("enum_values_capability pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    VALIDATION_LIST(*md, (*enum_values_capability));

    return SAI_STATUS_SUCCESS;
}

void meta_validation_post_sai_query_attribute_enum_values_capability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ sai_attr_id_t attr_id,
        _Inout_ sai_s32_list_t *enum_values_capability)
{
    SWSS_LOG_ENTER();

    auto& md = *sai_metadata_get_attr_metadata(object_type, attr_id);

    if (enum_values_capability->list == NULL)
        return;

    // check if returned values are valid
    for (uint32_t index = 0; index < enum_values_capability->count; index++)
    {
        int32_t value = enum_values_capability->list[index];

        if (!sai_metadata_is_allowed_enum_value(&md, value))
        {
            META_LOG_ERROR(md, "returned enum value %d is not allowed on %s:%s",
                    value,
                    md.attridname,
                    md.enummetadata->name);
        }
    }
}

sai_status_t meta_sai_query_attribute_enum_values_capability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ sai_attr_id_t attr_id,
        _Inout_ sai_s32_list_t *enum_values_capability,
        _In_ sai_query_attribute_enum_values_capability_fn get)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_validate_sai_query_attribute_enum_values_capability(
            switch_id,
            object_type,
            attr_id,
            enum_values_capability);

    if (status != SAI_STATUS_SUCCESS)
        return status;

    // execute actual API

    status = get(switch_id, object_type, attr_id, enum_values_capability);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_validation_post_sai_query_attribute_enum_values_capability(
                    switch_id,
                    object_type,
                    attr_id,
                    enum_values_capability);
    }

    return status;
}

static sai_status_t meta_validate_sai_object_type_get_availability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Out_ uint64_t *count)
{
    SWSS_LOG_ENTER();

    // check if switch_exists
    sai_object_meta_key_t meta_key = { .objecttype = SAI_OBJECT_TYPE_SWITCH, .objectkey = { .key = { .object_id = switch_id } } };

    if (!g_saiObjectCollection.objectExists(meta_key))
    {
        SWSS_LOG_ERROR("switch %s don't exists",
                sai_serialize_object_id(switch_id).c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if switch has correct object type
    sai_object_type_t ot = sai_object_type_query(switch_id);

    if (ot != SAI_OBJECT_TYPE_SWITCH)
    {
        SWSS_LOG_ERROR("switch_id %s must be object type SWITCH, but is %s",
                sai_serialize_object_id(switch_id).c_str(),
                sai_serialize_object_type(ot).c_str());

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check specified object type
    if (object_type <= SAI_OBJECT_TYPE_NULL ||
            object_type >= SAI_OBJECT_TYPE_EXTENSIONS_MAX)
    {
        SWSS_LOG_ERROR("invalid object type specified: %d, FIXME", object_type);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check attr count
    if (attr_count == 0 || attr_count > MAX_LIST_COUNT)
    {
        SWSS_LOG_ERROR("invalid attr_count %u", attr_count);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (attr_list == NULL)
    {
        SWSS_LOG_ERROR("attr_list is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // check if attr ids are valid
    for (uint32_t index = 0; index < attr_count; index++)
    {
        auto *md = sai_metadata_get_attr_metadata(object_type, attr_list[index].id);

        if (md == NULL)
        {
            SWSS_LOG_ERROR("failed to get metadata for object type %s and attr id %d",
                    sai_serialize_object_type(object_type).c_str(),
                    attr_list[index].id);

            return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    // check count
    if (count == NULL)
    {
        SWSS_LOG_ERROR("count pointer is NULL");

        return SAI_STATUS_INVALID_PARAMETER;
    }

    // TODO if attr values are needed, they need to be checked

    return SAI_STATUS_SUCCESS;
}

static void meta_validation_post_sai_object_type_get_availability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Out_ uint64_t *count)
{
    SWSS_LOG_ENTER();

    // TODO if attr values are needed, they need to be checked
}

sai_status_t meta_sai_object_type_get_availability(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_type_t object_type,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list,
        _Out_ uint64_t *count,
        _In_ sai_object_type_get_availability_fn get)
{
    SWSS_LOG_ENTER();

    sai_status_t status = meta_validate_sai_object_type_get_availability(
            switch_id,
            object_type,
            attr_count,
            attr_list,
            count);

    if (status != SAI_STATUS_SUCCESS)
        return status;

    // execute actual API

    status = get(switch_id, object_type, attr_count, attr_list, count);

    if (status == SAI_STATUS_SUCCESS)
    {
        meta_validation_post_sai_object_type_get_availability(
            switch_id,
            object_type,
            attr_count,
            attr_list,
            count);
    }

    return status;
}
