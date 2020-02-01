#include "VirtualOidTranslator.h"
#include "VirtualObjectIdManager.h"

#include "swss/logger.h"
#include "meta/sai_serialize.h"

#include <inttypes.h>

#include "syncd.h" // TODO to be removed

using namespace syncd;

// TODO move all redis access to db object connector

extern bool isInitViewMode(); // TODO move to shared module
extern std::shared_ptr<sairedis::VirtualObjectIdManager> g_virtualObjectIdManager;

sai_object_id_t VirtualOidTranslator::translateRidToVid(
        _In_ sai_object_id_t rid,
        _In_ sai_object_id_t switchVid)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(m_mutex);

    /*
     * NOTE: switch_vid here is Virtual ID of switch for which we need
     * create VID for given RID.
     */

    if (rid == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_DEBUG("translated RID null to VID null");

        return SAI_NULL_OBJECT_ID;
    }

    auto it = m_rid2vid.find(rid);

    if (it != m_rid2vid.end())
    {
        return it->second;
    }

    sai_object_id_t vid;

    std::string strRid = sai_serialize_object_id(rid);

    auto pvid = g_redisClient->hget(RIDTOVID, strRid); // TODO to db object

    if (pvid != NULL)
    {
        /*
         * Object exists.
         */

        std::string strVid = *pvid;

        sai_deserialize_object_id(strVid, vid);

        SWSS_LOG_DEBUG("translated RID 0x%" PRIx64 " to VID 0x%" PRIx64, rid, vid);

        return vid;
    }

    SWSS_LOG_DEBUG("spotted new RID 0x%" PRIx64, rid);

    sai_object_type_t object_type = g_vendorSai->objectTypeQuery(rid); // TODO move to std::function or wrapper class

    if (object_type == SAI_OBJECT_TYPE_NULL)
    {
        SWSS_LOG_THROW("g_vendorSai->objectTypeQuery returned NULL type for RID 0x%" PRIx64, rid);
    }

    if (object_type == SAI_OBJECT_TYPE_SWITCH)
    {
        /*
         * Switch ID should be already inside local db or redis db when we
         * created switch, so we should never get here.
         */

        SWSS_LOG_THROW("RID 0x%" PRIx64 " is switch object, but not in local or redis db, bug!", rid);
    }

    vid = g_virtualObjectIdManager->allocateNewObjectId(object_type, switchVid); // TODO to std::function or separate object

    SWSS_LOG_DEBUG("translated RID 0x%" PRIx64 " to VID 0x%" PRIx64, rid, vid);

    std::string strVid = sai_serialize_object_id(vid);

    // TODO same as insertRidAndVid

    g_redisClient->hset(RIDTOVID, strRid, strVid); // TODO to db object
    g_redisClient->hset(VIDTORID, strVid, strRid);

    m_rid2vid[rid] = vid;
    m_vid2rid[vid] = rid;

    return vid;
}

bool VirtualOidTranslator::checkRidExists(
        _In_ sai_object_id_t rid)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(m_mutex);

    if (rid == SAI_NULL_OBJECT_ID)
        return true;

    if (m_rid2vid.find(rid) != m_rid2vid.end())
        return true;

    std::string strRid = sai_serialize_object_id(rid);

    auto pvid = g_redisClient->hget(RIDTOVID, strRid); // TODO use db object

    if (pvid != NULL)
        return true;

    return false;
}

void VirtualOidTranslator::translateRidToVid(
        _Inout_ sai_object_list_t &element,
        _In_ sai_object_id_t switchVid)
{
    SWSS_LOG_ENTER();

    for (uint32_t i = 0; i < element.count; i++)
    {
        element.list[i] = translateRidToVid(element.list[i], switchVid);
    }
}

void VirtualOidTranslator::translateRidToVid(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t switchVid,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attrList)
{
    SWSS_LOG_ENTER();

    /*
     * We receive real id's here, if they are new then create new VIDs for them
     * and put in db, if entry exists in db, use it.
     *
     * NOTE: switch_id is VID of switch on which those RIDs are provided.
     */

    for (uint32_t i = 0; i < attr_count; i++)
    {
        sai_attribute_t &attr = attrList[i];

        auto meta = sai_metadata_get_attr_metadata(objectType, attr.id);

        if (meta == NULL)
        {
            SWSS_LOG_THROW("unable to get metadata for object type %x, attribute %d", objectType, attr.id);
        }

        /*
         * TODO: Many times we do switch for list of attributes to perform some
         * operation on each oid from that attribute, we should provide clever
         * way via sai metadata utils to get that.
         */

        switch (meta->attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:
                attr.value.oid = translateRidToVid(attr.value.oid, switchVid);
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:
                translateRidToVid(attr.value.objlist, switchVid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:
                if (attr.value.aclfield.enable)
                    attr.value.aclfield.data.oid = translateRidToVid(attr.value.aclfield.data.oid, switchVid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:
                if (attr.value.aclfield.enable)
                    translateRidToVid(attr.value.aclfield.data.objlist, switchVid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:
                if (attr.value.aclaction.enable)
                    attr.value.aclaction.parameter.oid = translateRidToVid(attr.value.aclaction.parameter.oid, switchVid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:
                if (attr.value.aclaction.enable)
                    translateRidToVid(attr.value.aclaction.parameter.objlist, switchVid);
                break;

            default:

                /*
                 * If in future new attribute with object id will be added this
                 * will make sure that we will need to add handler here.
                 */

                if (meta->isoidattribute)
                {
                    SWSS_LOG_THROW("attribute %s is object id, but not processed, FIXME", meta->attridname);
                }

                break;
        }
    }
}

sai_object_id_t VirtualOidTranslator::translateVidToRid(
        _In_ sai_object_id_t vid)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(m_mutex);

    if (vid == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_DEBUG("translated VID null to RID null");

        return SAI_NULL_OBJECT_ID;
    }

    auto it = m_vid2rid.find(vid);

    if (it != m_vid2rid.end())
    {
        return it->second;
    }

    std::string strVid = sai_serialize_object_id(vid);

    std::string strRid;

    auto prid = g_redisClient->hget(VIDTORID, strVid); // TODO to db object

    if (prid == NULL)
    {
        if (isInitViewMode())
        {
            /*
             * If user created object that is object id, then it should not
             * query attributes of this object in init view mode, because he
             * knows all attributes passed to that object.
             *
             * NOTE: This may be a problem for some objects in init view mode.
             * We will need to revisit this after checking with real SAI
             * implementation.  Problem here may be that user will create some
             * object and actually will need to to query some of it's values,
             * like buffer limitations etc, mostly probably this will happen on
             * SWITCH object.
             */

            SWSS_LOG_THROW("can't get RID in init view mode - don't query created objects");
        }

        SWSS_LOG_THROW("unable to get RID for VID: 0x%" PRIx64, vid);
    }

    strRid = *prid;

    sai_object_id_t rid;

    sai_deserialize_object_id(strRid, rid);

    /*
     * We got this RID from redis db, so put it also to local db so it will be
     * faster to retrieve it late on.
     */

    m_vid2rid[vid] = rid;

    SWSS_LOG_DEBUG("translated VID 0x%" PRIx64 " to RID 0x%" PRIx64, vid, rid);

    return rid;
}

/*
 * NOTE: We could have in metadata utils option to execute function on each
 * object on oid like this.  Problem is that we can't then add extra
 * parameters.
 */

bool VirtualOidTranslator::tryTranslateVidToRid(
        _In_ sai_object_id_t vid,
        _Out_ sai_object_id_t& rid)
{
    SWSS_LOG_ENTER();

    try
    {
        rid = translateVidToRid(vid);
        return true;
    }
    catch (const std::exception& e)
    {
        // message was logged already when throwing
        return false;
    }
}

void VirtualOidTranslator::translateVidToRid(
        _Inout_ sai_object_list_t &element)
{
    SWSS_LOG_ENTER();

    for (uint32_t i = 0; i < element.count; i++)
    {
        element.list[i] = translateVidToRid(element.list[i]);
    }
}

void VirtualOidTranslator::translateVidToRid(
        _In_ sai_object_type_t objectType,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attrList)
{
    SWSS_LOG_ENTER();

    /*
     * All id's received from sairedis should be virtual, so lets translate
     * them to real id's before we execute actual api.
     */

    for (uint32_t i = 0; i < attr_count; i++)
    {
        sai_attribute_t &attr = attrList[i];

        auto meta = sai_metadata_get_attr_metadata(objectType, attr.id);

        if (meta == NULL)
        {
            SWSS_LOG_THROW("unable to get metadata for object type %x, attribute %d", objectType, attr.id);
        }

        switch (meta->attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:
                attr.value.oid = translateVidToRid(attr.value.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:
                translateVidToRid(attr.value.objlist);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:
                if (attr.value.aclfield.enable)
                    attr.value.aclfield.data.oid = translateVidToRid(attr.value.aclfield.data.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:
                if (attr.value.aclfield.enable)
                    translateVidToRid(attr.value.aclfield.data.objlist);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:
                if (attr.value.aclaction.enable)
                    attr.value.aclaction.parameter.oid = translateVidToRid(attr.value.aclaction.parameter.oid);
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:
                if (attr.value.aclaction.enable)
                    translateVidToRid(attr.value.aclaction.parameter.objlist);
                break;

            default:

                /*
                 * If in future new attribute with object id will be added this
                 * will make sure that we will need to add handler here.
                 */

                if (meta->isoidattribute)
                {
                    SWSS_LOG_THROW("attribute %s is object id, but not processed, FIXME", meta->attridname);
                }

                break;
        }
    }
}

void VirtualOidTranslator::translateVidToRid(
        _Inout_ sai_object_meta_key_t &metaKey)
{
    SWSS_LOG_ENTER();

    auto info = sai_metadata_get_object_type_info(metaKey.objecttype);

    if (info->isobjectid)
    {
        metaKey.objectkey.key.object_id =
            translateVidToRid(metaKey.objectkey.key.object_id);

        return;
    }

    for (size_t j = 0; j < info->structmemberscount; ++j)
    {
        const sai_struct_member_info_t *m = info->structmembers[j];

        if (m->membervaluetype == SAI_ATTR_VALUE_TYPE_OBJECT_ID)
        {
            sai_object_id_t vid = m->getoid(&metaKey);

            sai_object_id_t rid = translateVidToRid(vid);

            m->setoid(&metaKey, rid);
        }
    }
}

void VirtualOidTranslator::insertRidAndVid(
        _In_ sai_object_id_t rid,
        _In_ sai_object_id_t vid)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(m_mutex);

    /*
     * TODO: This must be ATOMIC.
     *
     * To support multiple switches vid/rid map must be per switch.
     */

    m_rid2vid[rid] = vid;
    m_vid2rid[vid] = rid;

    auto strVid = sai_serialize_object_id(vid);
    auto strRid = sai_serialize_object_id(rid);

    g_redisClient->hset(VIDTORID, strVid, strRid);
    g_redisClient->hset(RIDTOVID, strRid, strVid);
}

void VirtualOidTranslator::eraseRidAndVid(
        _In_ sai_object_id_t rid,
        _In_ sai_object_id_t vid)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(m_mutex);

    /*
     * TODO: This must be ATOMIC.
     */

    auto strVid = sai_serialize_object_id(vid);
    auto strRid = sai_serialize_object_id(rid);

    g_redisClient->hdel(VIDTORID, strVid);
    g_redisClient->hdel(RIDTOVID, strRid);

    // remove from local vid2rid and rid2vid map

    m_rid2vid.erase(rid);
    m_vid2rid.erase(vid);
}

void VirtualOidTranslator::clearLocalCache()
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(m_mutex);

    m_rid2vid.clear();
    m_vid2rid.clear();
}
