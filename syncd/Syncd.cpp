#include "Syncd.h"
#include "VidManager.h"

#include "lib/inc/sairediscommon.h"

#include "swss/logger.h"
#include "swss/tokenize.h"

#include "meta/sai_serialize.h"

#include <iterator>
#include <algorithm>

// TODO mutex must be used in 3 places
// - notification processing
// - main event loop processing
// - syncd hard init when switches are created
//   (notifications could be sent during that)

#include "syncd.h" // TODO to be removed

sai_status_t notifySyncd(
        _In_ const std::string& op);

sai_status_t processQuadEvent(
        _In_ sai_common_api_t api,
        _In_ const swss::KeyOpFieldsValuesTuple &kco);

sai_status_t processBulkEntry(
        _In_ sai_object_type_t objectType,
        _In_ const std::vector<std::string> &objectIds,
        _In_ sai_common_api_t api,
        _In_ const std::vector<std::shared_ptr<SaiAttributeList>> &attributes);

sai_status_t processBulkOid(
        _In_ sai_object_type_t objectType,
        _In_ const std::vector<std::string> &objectIds,
        _In_ sai_common_api_t api,
        _In_ const std::vector<std::shared_ptr<SaiAttributeList>> &attributes);

void sendApiResponse(
        _In_ sai_common_api_t api,
        _In_ sai_status_t status,
        _In_ uint32_t object_count = 0,
        _In_ sai_status_t * object_statuses = NULL);

sai_status_t processOid(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ sai_common_api_t api,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list);

void sendGetResponse(
        _In_ sai_object_type_t object_type,
        _In_ const std::string &str_object_id,
        _In_ sai_object_id_t switch_id,
        _In_ sai_status_t status,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list);

void on_switch_create_in_init_view(
        _In_ sai_object_id_t switch_vid,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list);

using namespace syncd;

Syncd::Syncd(
        _In_ std::shared_ptr<CommandLineOptions> cmd,
        _In_ bool isWarmStart):
    m_commandLineOptions(cmd),
    m_isWarmStart(isWarmStart),
    m_asicInitViewMode(false) // by default we are in APPLY view mode
{
    SWSS_LOG_ENTER();

    m_manager = std::make_shared<FlexCounterManager>();
}

Syncd::~Syncd()
{
    SWSS_LOG_ENTER();

    // empty
}

bool Syncd::getAsicInitViewMode() const
{
    SWSS_LOG_ENTER();

    return m_asicInitViewMode;
}

void Syncd::setAsicInitViewMode(
        _In_ bool enable)
{
    SWSS_LOG_ENTER();

    m_asicInitViewMode = enable;
}

bool Syncd::isInitViewMode() const
{
    SWSS_LOG_ENTER();

    return m_asicInitViewMode && m_commandLineOptions->m_enableTempView;
}

void Syncd::processEvent(
        _In_ swss::ConsumerTable &consumer)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(g_mutex); // TODO

    do
    {
        swss::KeyOpFieldsValuesTuple kco;

        if (isInitViewMode())
        {
            /*
             * In init mode we put all data to TEMP view and we snoop.  We need
             * to specify temporary view prefix in consumer since consumer puts
             * data to redis db.
             */

            consumer.pop(kco, TEMP_PREFIX);
        }
        else
        {
            consumer.pop(kco);
        }

        processSingleEvent(kco);
    }
    while (!consumer.empty());
}

sai_status_t Syncd::processSingleEvent(
        _In_ const swss::KeyOpFieldsValuesTuple &kco)
{
    SWSS_LOG_ENTER();

    auto& key = kfvKey(kco);
    auto& op = kfvOp(kco);

    SWSS_LOG_INFO("key: %s op: %s", key.c_str(), op.c_str());

    if (key.length() == 0)
    {
        SWSS_LOG_DEBUG("no elements in m_buffer");

        return SAI_STATUS_SUCCESS;
    }

    if (op == REDIS_ASIC_STATE_COMMAND_CREATE)
        return processQuadEvent(SAI_COMMON_API_CREATE, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_REMOVE)
        return processQuadEvent(SAI_COMMON_API_REMOVE, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_SET)
        return processQuadEvent(SAI_COMMON_API_SET, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_GET)
        return processQuadEvent(SAI_COMMON_API_GET, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_BULK_CREATE)
        return processBulkQuadEvent(SAI_COMMON_API_BULK_CREATE, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_BULK_REMOVE)
        return processBulkQuadEvent(SAI_COMMON_API_BULK_REMOVE, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_BULK_SET)
        return processBulkQuadEvent(SAI_COMMON_API_BULK_SET, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_NOTIFY)
        return notifySyncd(key);

    if (op == REDIS_ASIC_STATE_COMMAND_GET_STATS)
        return processGetStatsEvent(kco);

    if (op == REDIS_ASIC_STATE_COMMAND_CLEAR_STATS)
        return processClearStatsEvent(kco);

    if (op == REDIS_ASIC_STATE_COMMAND_FLUSH)
        return processFdbFlush(kco);

    if (op == REDIS_ASIC_STATE_COMMAND_ATTR_ENUM_VALUES_CAPABILITY_QUERY)
        return processAttrEnumValuesCapabilityQuery(kco);

    if (op == REDIS_ASIC_STATE_COMMAND_OBJECT_TYPE_GET_AVAILABILITY_QUERY)
        return processObjectTypeGetAvailabilityQuery(kco);

    SWSS_LOG_THROW("event op '%s' is not implemented, FIXME", op.c_str());
}

sai_status_t Syncd::processAttrEnumValuesCapabilityQuery(
        _In_ const swss::KeyOpFieldsValuesTuple &kco)
{
    SWSS_LOG_ENTER();

    auto& strSwitchVid = kfvKey(kco);

    sai_object_id_t switchVid;
    sai_deserialize_object_id(strSwitchVid, switchVid);

    sai_object_id_t switchRid = g_translator->translateVidToRid(switchVid);

    auto& values = kfvFieldsValues(kco);

    if (values.size() != 3)
    {
        SWSS_LOG_ERROR("Invalid input: expected 3 arguments, received %zu", values.size());

        m_getResponse->set(sai_serialize_status(SAI_STATUS_INVALID_PARAMETER), {}, REDIS_ASIC_STATE_COMMAND_ATTR_ENUM_VALUES_CAPABILITY_RESPONSE);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_type_t objectType;
    sai_deserialize_object_type(fvValue(values[0]), objectType);

    sai_attr_id_t attr_id;
    sai_deserialize_attr_id(fvValue(values[1]), attr_id);

    uint32_t list_size = std::stoi(fvValue(values[2]));

    std::vector<int32_t> enum_capabilities_list(list_size);

    sai_s32_list_t enumCapList;

    enumCapList.count = list_size;
    enumCapList.list = enum_capabilities_list.data();

    sai_status_t status = g_vendorSai->queryAattributeEnumValuesCapability(switchRid, objectType, attr_id, &enumCapList);

    std::vector<swss::FieldValueTuple> entry;

    if (status == SAI_STATUS_SUCCESS)
    {
        std::vector<std::string> vec;
        std::transform(enumCapList.list, enumCapList.list + enumCapList.count,
                std::back_inserter(vec), [](auto&e) { return std::to_string(e); });

        std::ostringstream join;
        std::copy(vec.begin(), vec.end(), std::ostream_iterator<std::string>(join, ","));

        auto strCap = join.str();

        entry =
        {
            swss::FieldValueTuple("ENUM_CAPABILITIES", strCap),
            swss::FieldValueTuple("ENUM_COUNT", std::to_string(enumCapList.count))
        };

        SWSS_LOG_DEBUG("Sending response: capabilities = '%s', count = %d", strCap.c_str(), enumCapList.count);
    }

    m_getResponse->set(sai_serialize_status(status), entry, REDIS_ASIC_STATE_COMMAND_ATTR_ENUM_VALUES_CAPABILITY_RESPONSE);

    return status;
}

sai_status_t Syncd::processObjectTypeGetAvailabilityQuery(
    _In_ const swss::KeyOpFieldsValuesTuple &kco)
{
    SWSS_LOG_ENTER();

    auto& strSwitchVid = kfvKey(kco);

    sai_object_id_t switchVid;
    sai_deserialize_object_id(strSwitchVid, switchVid);

    const sai_object_id_t switchRid = g_translator->translateVidToRid(switchVid);

    std::vector<swss::FieldValueTuple> values = kfvFieldsValues(kco);

    // Syncd needs to pop the object type off the end of the list in order to
    // retrieve the attribute list

    sai_object_type_t objectType;
    sai_deserialize_object_type(fvValue(values.back()), objectType);

    values.pop_back();

    SaiAttributeList list(objectType, values, false);

    sai_attribute_t *attr_list = list.get_attr_list();

    uint32_t attr_count = list.get_attr_count();

    g_translator->translateVidToRid(objectType, attr_count, attr_list);

    uint64_t count;

    sai_status_t status = g_vendorSai->objectTypeGetAvailability(
            switchRid,
            objectType,
            attr_count,
            attr_list,
            &count);

    std::vector<swss::FieldValueTuple> entry;

    if (status == SAI_STATUS_SUCCESS)
    {
        entry.push_back(swss::FieldValueTuple("OBJECT_COUNT", std::to_string(count)));

        SWSS_LOG_DEBUG("Sending response: count = %lu", count);
    }

    m_getResponse->set(sai_serialize_status(status), entry, REDIS_ASIC_STATE_COMMAND_OBJECT_TYPE_GET_AVAILABILITY_RESPONSE);

    return status;
}

sai_status_t Syncd::processFdbFlush(
        _In_ const swss::KeyOpFieldsValuesTuple &kco)
{
    SWSS_LOG_ENTER();

    auto& key = kfvKey(kco);
    auto strSwitchVid = key.substr(key.find(":") + 1);

    sai_object_id_t switchVid;
    sai_deserialize_object_id(strSwitchVid, switchVid);

    sai_object_id_t switchRid = g_translator->translateVidToRid(switchVid);

    auto& values = kfvFieldsValues(kco);

    for (const auto &v: values)
    {
        SWSS_LOG_DEBUG("attr: %s: %s", fvField(v).c_str(), fvValue(v).c_str());
    }

    SaiAttributeList list(SAI_OBJECT_TYPE_FDB_FLUSH, values, false);

    /*
     * Attribute list can't be const since we will use it to translate VID to
     * RID in place.
     */

    sai_attribute_t *attr_list = list.get_attr_list();
    uint32_t attr_count = list.get_attr_count();

    g_translator->translateVidToRid(SAI_OBJECT_TYPE_FDB_FLUSH, attr_count, attr_list);

    sai_status_t status = g_vendorSai->flushFdbEntries(switchRid, attr_count, attr_list);

    m_getResponse->set(sai_serialize_status(status), {} , REDIS_ASIC_STATE_COMMAND_FLUSHRESPONSE);

    return status;
}

sai_status_t Syncd::processClearStatsEvent(
        _In_ const swss::KeyOpFieldsValuesTuple &kco)
{
    SWSS_LOG_ENTER();

    const std::string &key = kfvKey(kco);

    sai_object_meta_key_t metaKey;
    sai_deserialize_object_meta_key(key, metaKey);

    g_translator->translateVidToRid(metaKey);

    auto info = sai_metadata_get_object_type_info(metaKey.objecttype);

    if (info->isnonobjectid)
    {
        SWSS_LOG_THROW("non object id not supported on clear stats: %s, FIXME", key.c_str());
    }

    std::vector<sai_stat_id_t> counter_ids;

    for (auto&v: kfvFieldsValues(kco))
    {
        int32_t val;
        sai_deserialize_enum(fvField(v), info->statenum, val);

        counter_ids.push_back(val);
    }

    auto status = g_vendorSai->clearStats(
            metaKey.objecttype,
            metaKey.objectkey.key.object_id,
            (uint32_t)counter_ids.size(),
            counter_ids.data());

    m_getResponse->set(sai_serialize_status(status), {}, REDIS_ASIC_STATE_COMMAND_GETRESPONSE);

    return status;
}

sai_status_t Syncd::processGetStatsEvent(
        _In_ const swss::KeyOpFieldsValuesTuple &kco)
{
    SWSS_LOG_ENTER();

    const std::string &key = kfvKey(kco);

    sai_object_meta_key_t metaKey;
    sai_deserialize_object_meta_key(key, metaKey);

    // TODO get stats on created object in init view mode could fail

    g_translator->translateVidToRid(metaKey);

    auto info = sai_metadata_get_object_type_info(metaKey.objecttype);

    if (info->isnonobjectid)
    {
        SWSS_LOG_THROW("non object id not supported on clear stats: %s, FIXME", key.c_str());
    }

    std::vector<sai_stat_id_t> counter_ids;

    for (auto&v: kfvFieldsValues(kco))
    {
        int32_t val;
        sai_deserialize_enum(fvField(v), info->statenum, val);

        counter_ids.push_back(val);
    }

    std::vector<uint64_t> result;

    auto status = g_vendorSai->getStats(
            metaKey.objecttype,
            metaKey.objectkey.key.object_id,
            (uint32_t)counter_ids.size(),
            counter_ids.data(),
            result.data());

    std::vector<swss::FieldValueTuple> entry;

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to get stats");
    }
    else
    {
        const auto& values = kfvFieldsValues(kco);

        for (size_t i = 0; i < values.size(); i++)
        {
            entry.emplace_back(fvField(values[i]), std::to_string(result[i]));
        }
    }

    m_getResponse->set(sai_serialize_status(status), entry, REDIS_ASIC_STATE_COMMAND_GETRESPONSE);

    return status;
}

sai_status_t Syncd::processBulkQuadEvent(
        _In_ sai_common_api_t api,
        _In_ const swss::KeyOpFieldsValuesTuple &kco)
{
    SWSS_LOG_ENTER();

    if (isInitViewMode())
    {
        SWSS_LOG_THROW("bulk api (%s) is not supported in init view mode, FIXME",
                sai_serialize_common_api(api).c_str());
    }

    const std::string& key = kfvKey(kco); // objectType:count

    std::string strObjectType = key.substr(0, key.find(":"));

    sai_object_type_t objectType;
    sai_deserialize_object_type(strObjectType, objectType);

    const std::vector<swss::FieldValueTuple> &values = kfvFieldsValues(kco);

    // field = objectId
    // value = attrid=attrvalue|...

    std::vector<std::string> objectIds;

    std::vector<std::shared_ptr<SaiAttributeList>> attributes;

    for (const auto &fvt: values)
    {
        std::string strObjectId = fvField(fvt);
        std::string joined = fvValue(fvt);

        // decode values

        auto v = swss::tokenize(joined, '|');

        objectIds.push_back(strObjectId);

        std::vector<swss::FieldValueTuple> entries; // attributes per object id

        for (size_t i = 0; i < v.size(); ++i)
        {
            const std::string item = v.at(i);

            auto start = item.find_first_of("=");

            auto field = item.substr(0, start);
            auto value = item.substr(start + 1);

            entries.emplace_back(field, value);
        }

        // since now we converted this to proper list, we can extract attributes

        auto list = std::make_shared<SaiAttributeList>(objectType, entries, false);

        attributes.push_back(list);
    }

    SWSS_LOG_NOTICE("bulk %s execute with %zu items",
            strObjectType.c_str(),
            objectIds.size());

    if (api != SAI_COMMON_API_BULK_GET)
    {
        // translate attributes for all objects

        for (auto &list: attributes)
        {
            sai_attribute_t *attr_list = list->get_attr_list();
            uint32_t attr_count = list->get_attr_count();

            g_translator->translateVidToRid(objectType, attr_count, attr_list);
        }
    }

    auto info = sai_metadata_get_object_type_info(objectType);

    if (info->isobjectid)
    {
        return processBulkOid(objectType, objectIds, api, attributes);
    }
    else
    {
        return processBulkEntry(objectType, objectIds, api, attributes);
    }
}

sai_status_t Syncd::processBulkEntry(
        _In_ sai_object_type_t objectType,
        _In_ const std::vector<std::string> &objectIds,
        _In_ sai_common_api_t api,
        _In_ const std::vector<std::shared_ptr<SaiAttributeList>> &attributes)
{
    SWSS_LOG_ENTER();

    auto info = sai_metadata_get_object_type_info(objectType);

    if (info->isobjectid)
    {
        SWSS_LOG_THROW("passing oid object to bulk non obejct id operation");
    }

    // vendor SAI don't bulk API yet, so execute one by one

    std::vector<sai_status_t> statuses(objectIds.size());

    sai_status_t all = SAI_STATUS_SUCCESS;

    for (size_t idx = 0; idx < objectIds.size(); ++idx)
    {
        sai_object_meta_key_t meta_key;

        meta_key.objecttype = objectType;

        switch (objectType)
        {
            case SAI_OBJECT_TYPE_ROUTE_ENTRY:
                sai_deserialize_route_entry(objectIds[idx], meta_key.objectkey.key.route_entry);
                break;

            case SAI_OBJECT_TYPE_FDB_ENTRY:
                sai_deserialize_fdb_entry(objectIds[idx], meta_key.objectkey.key.fdb_entry);
                break;

            default:
                SWSS_LOG_THROW("object %s not implemented, FIXME", sai_serialize_object_type(objectType).c_str());
        }

        sai_status_t status = SAI_STATUS_FAILURE;

        auto& list = attributes[idx];

        sai_attribute_t *attr_list = list->get_attr_list();
        uint32_t attr_count = list->get_attr_count();

        if (api == SAI_COMMON_API_BULK_CREATE)
        {
            status = processEntry(meta_key, SAI_COMMON_API_CREATE, attr_count, attr_list);
        }
        else if (api == SAI_COMMON_API_BULK_REMOVE)
        {
            status = processEntry(meta_key, SAI_COMMON_API_REMOVE, attr_count, attr_list);
        }
        else if (api == SAI_COMMON_API_BULK_SET)
        {
            status = processEntry(meta_key, SAI_COMMON_API_SET, attr_count, attr_list);
        }
        else
        {
            SWSS_LOG_THROW("api %d is not supported in bulk mode", api);
        }

        if (api != SAI_COMMON_API_BULK_GET && status != SAI_STATUS_SUCCESS)
        {
            if (!m_commandLineOptions->m_enableSyncMode)
            {
                SWSS_LOG_THROW("operation %s for %s failed in async mode!",
                        sai_serialize_common_api(api).c_str(),
                        sai_serialize_object_type(objectType).c_str());
            }

            all = SAI_STATUS_FAILURE; // all can be success if all has been success
        }

        statuses[idx] = status;
    }

    sendApiResponse(api, all, (uint32_t)objectIds.size(), statuses.data());

    return all;
}

sai_status_t Syncd::processEntry(
        _In_ sai_object_meta_key_t &meta_key,
        _In_ sai_common_api_t api,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    g_translator->translateVidToRid(meta_key);

    switch (api)
    {
        case SAI_COMMON_API_CREATE:
            return g_vendorSai->create(meta_key, SAI_NULL_OBJECT_ID, attr_count, attr_list);

        case SAI_COMMON_API_REMOVE:
            return g_vendorSai->remove(meta_key);

        case SAI_COMMON_API_SET:
            return g_vendorSai->set(meta_key, attr_list);

        case SAI_COMMON_API_GET:
            return g_vendorSai->get(meta_key, attr_count, attr_list);

        default:

            SWSS_LOG_THROW("api %s not supported", sai_serialize_common_api(api).c_str());
    }
}

sai_status_t Syncd::processBulkOid(
        _In_ sai_object_type_t objectType,
        _In_ const std::vector<std::string> &objectIds,
        _In_ sai_common_api_t api,
        _In_ const std::vector<std::shared_ptr<SaiAttributeList>> &attributes)
{
    SWSS_LOG_ENTER();

    auto info = sai_metadata_get_object_type_info(objectType);

    if (info->isnonobjectid)
    {
        SWSS_LOG_THROW("passing non object id to bulk oid obejct operation");
    }

    // vendor SAI don't bulk API yet, so execute one by one

    std::vector<sai_status_t> statuses(objectIds.size());

    sai_status_t all = SAI_STATUS_SUCCESS;

    for (size_t idx = 0; idx < objectIds.size(); ++idx)
    {
        sai_status_t status = SAI_STATUS_FAILURE;

        auto& list = attributes[idx];

        sai_attribute_t *attr_list = list->get_attr_list();
        uint32_t attr_count = list->get_attr_count();

        if (api == SAI_COMMON_API_BULK_CREATE)
        {
            status = processOid(objectType, objectIds[idx], SAI_COMMON_API_CREATE, attr_count, attr_list);
        }
        else if (api == SAI_COMMON_API_BULK_REMOVE)
        {
            status = processOid(objectType, objectIds[idx], SAI_COMMON_API_REMOVE, attr_count, attr_list);
        }
        else if (api == SAI_COMMON_API_BULK_SET)
        {
            status = processOid(objectType, objectIds[idx], SAI_COMMON_API_SET, attr_count, attr_list);
        }
        else
        {
            SWSS_LOG_THROW("api %d is not supported in bulk mode", api);
        }

        if (status != SAI_STATUS_SUCCESS)
        {
            if (!m_commandLineOptions->m_enableSyncMode)
            {
                SWSS_LOG_THROW("operation %s for %s failed in async mode!",
                        sai_serialize_common_api(api).c_str(),
                        sai_serialize_object_type(objectType).c_str());
            }

            all = SAI_STATUS_FAILURE; // all can be success if all has been success
        }

        statuses[idx] = status;
    }

    sendApiResponse(api, all, (uint32_t)objectIds.size(), statuses.data());

    return all;
}

sai_status_t Syncd::processQuadEventInInitViewMode(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& strObjectId,
        _In_ sai_common_api_t api,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    /*
     * Since attributes are not checked, it may happen that user will send some
     * invalid VID in object id/list in attribute, metadata should handle that,
     * but if that happen, this id will be treated as "new" object instead of
     * existing one.
     */

    switch (api)
    {
        case SAI_COMMON_API_CREATE:
            return processQuadInInitViewModeCreate(objectType, strObjectId, attr_count, attr_list);

        case SAI_COMMON_API_REMOVE:
            return processQuadInInitViewModeRemove(objectType, strObjectId);

        case SAI_COMMON_API_SET:
            return processQuadInInitViewModeSet(objectType, strObjectId, attr_list);

        case SAI_COMMON_API_GET:
            return processQuadInInitViewModeGet(objectType, strObjectId, attr_count, attr_list);

        default:

            SWSS_LOG_THROW("common api (%s) is not implemented in init view mode", sai_serialize_common_api(api).c_str());
    }
}

sai_status_t Syncd::processQuadInInitViewModeCreate(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& strObjectId,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    if (objectType == SAI_OBJECT_TYPE_PORT)
    {
        /*
         * Reason for this is that if user will create port, new port is not
         * actually created so when for example querying new queues for new
         * created port, there are not there, since no actual port create was
         * issued on the ASIC.
         */

        SWSS_LOG_THROW("port object can't be created in init view mode");
    }

    auto info = sai_metadata_get_object_type_info(objectType);

    // we assume create of those non object id object types will succeed

    if (info->isobjectid)
    {
        sai_object_id_t objectVid;
        sai_deserialize_object_id(strObjectId, objectVid);

        /*
         * Object ID here is actual VID returned from redis during
         * creation this is floating VID in init view mode.
         */

        SWSS_LOG_DEBUG("generic create (init view) for %s, floating VID: %s",
                sai_serialize_object_type(objectType).c_str(),
                sai_serialize_object_id(objectVid).c_str());

        if (objectType == SAI_OBJECT_TYPE_SWITCH)
        {
            on_switch_create_in_init_view(objectVid, attr_count, attr_list);
        }
    }

    sendApiResponse(SAI_COMMON_API_CREATE, SAI_STATUS_SUCCESS);

    return SAI_STATUS_SUCCESS;
}

sai_status_t Syncd::processQuadInInitViewModeRemove(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& strObjectId)
{
    SWSS_LOG_ENTER();

    if (objectType == SAI_OBJECT_TYPE_PORT)
    {
        /*
         * Reason for this is that if user will remove port, actual resources
         * for it won't be released, lanes would be still occupied and there is
         * extra logic required in post port remove which clears OIDs
         * (ipgs,queues,SGs) from redis db that are automatically removed by
         * vendor SAI, and comparison logic don't support that.
         */

        SWSS_LOG_THROW("port object can't be removed in init view mode");
    }

    if (objectType == SAI_OBJECT_TYPE_SWITCH)
    {
        /*
         * NOTE: Special care needs to be taken to clear all this switch id's
         * from all db's currently we skip this since we assume that orchagent
         * will not be removing switches, just creating.  But it may happen
         * when asic will fail etc.
         *
         * To support multiple switches this case must be refactored.
         */

        SWSS_LOG_THROW("remove switch (%s) is not supported in init view mode yet! FIXME", strObjectId.c_str());
    }

    // NOTE: we should also prevent removing some other non removable objects

    auto info = sai_metadata_get_object_type_info(objectType);

    if (info->isobjectid)
    {
        /*
         * If object is existing object (like bridge port, vlan member) user
         * may want to remove them, but this is temporary view, and when we
         * receive apply view, we will populate existing objects to temporary
         * view (since not all of them user may query) and this will produce
         * conflict, since some of those objects user could explicitly remove.
         * So to solve that we need to have a list of removed objects, and then
         * only populate objects which not exist on removed list.
         */

        sai_object_id_t objectVid;
        sai_deserialize_object_id(strObjectId, objectVid);

        // this set may contain removed objects from multiple switches

        m_initViewRemovedVidSet.insert(objectVid);
    }

    sendApiResponse(SAI_COMMON_API_REMOVE, SAI_STATUS_SUCCESS);

    return SAI_STATUS_SUCCESS;
}

sai_status_t Syncd::processQuadInInitViewModeSet(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& strObjectId,
        _In_ sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    // we support SET api on all objects in init view mode

    sendApiResponse(SAI_COMMON_API_SET, SAI_STATUS_SUCCESS);

    return SAI_STATUS_SUCCESS;
}

sai_status_t Syncd::processQuadInInitViewModeGet(
        _In_ sai_object_type_t objectType,
        _In_ const std::string& strObjectId,
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    sai_status_t status;

    auto info = sai_metadata_get_object_type_info(objectType);

    sai_object_id_t switchVid = SAI_NULL_OBJECT_ID;

    if (info->isnonobjectid)
    {
        /*
         * Those objects are user created, so if user created ROUTE he
         * passed some attributes, there is no sense to support GET
         * since user explicitly know what attributes were set, similar
         * for other non object id types.
         */

        SWSS_LOG_ERROR("get is not supported on %s in init view mode", sai_serialize_object_type(objectType).c_str());

        status = SAI_STATUS_NOT_SUPPORTED;
    }
    else
    {
        sai_object_id_t objectVid;
        sai_deserialize_object_id(strObjectId, objectVid);

        switchVid = VidManager::switchIdQuery(objectVid);

        SWSS_LOG_DEBUG("generic get (init view) for object type %s:%s",
                sai_serialize_object_type(objectType).c_str(),
                strObjectId.c_str());

        /*
         * Object must exists, we can't call GET on created object
         * in init view mode, get here can be called on existing
         * objects like default trap group to get some vendor
         * specific values.
         *
         * Exception here is switch, since all switches must be
         * created, when user will create switch on init view mode,
         * switch will be matched with existing switch, or it will
         * be explicitly created so user can query it properties.
         *
         * Translate vid to rid will make sure that object exist
         * and it have RID defined, so we can query it.
         */

        sai_object_id_t rid = g_translator->translateVidToRid(objectVid);

        sai_object_meta_key_t meta_key;

        meta_key.objecttype = objectType;
        meta_key.objectkey.key.object_id = rid;

        status = g_vendorSai->get(meta_key, attr_count, attr_list);
    }

    /*
     * We are in init view mode, but ether switch already existed or first
     * command was creating switch and user created switch.
     *
     * We could change that later on, depends on object type we can extract
     * switch id, we could also have this method inside metadata to get meta
     * key.
     */

    sendGetResponse(objectType, strObjectId, switchVid, status, attr_count, attr_list);

    return status;
}

void Syncd::sendApiResponse(
        _In_ sai_common_api_t api,
        _In_ sai_status_t status,
        _In_ uint32_t object_count,
        _In_ sai_status_t* object_statuses)
{
    SWSS_LOG_ENTER();

    /*
     * By default synchronous mode is disabled and can be enabled by command
     * line on syncd start. This will also require to enable synchronous mode
     * in OA/sairedis because same GET RESPONSE channel is used to generate
     * response for sairedis quad API.
     */

    if (!m_commandLineOptions->m_enableSyncMode)
    {
        return;
    }

    switch (api)
    {
        case SAI_COMMON_API_CREATE:
        case SAI_COMMON_API_REMOVE:
        case SAI_COMMON_API_SET:
        case SAI_COMMON_API_BULK_CREATE:
        case SAI_COMMON_API_BULK_REMOVE:
        case SAI_COMMON_API_BULK_SET:
            break;

        default:
            SWSS_LOG_THROW("api %s not supported by this function",
                    sai_serialize_common_api(api).c_str());
    }

    if (status != SAI_STATUS_SUCCESS)
    {
        /*
         * TODO: If api fill fail in sync mode, we need to take some actions to
         * handle that, and those are not trivial tasks:
         *
         * - in case create fail, remove object from redis database
         * - in case remove fail, bring back removed object to database
         * - in case set fail, bring back previous value if it was set
         */

        SWSS_LOG_THROW("api %s failed in syncd mode: %s, FIXME",
                    sai_serialize_common_api(api).c_str(),
                    sai_serialize_status(status).c_str());
    }

    std::vector<swss::FieldValueTuple> entry;

    for (uint32_t idx = 0; idx < object_count; idx++)
    {
        swss::FieldValueTuple fvt(sai_serialize_status(object_statuses[idx]), "");

        entry.push_back(fvt);
    }

    std::string strStatus = sai_serialize_status(status);

    SWSS_LOG_INFO("sending response for %s api with status: %s",
            sai_serialize_common_api(api).c_str(),
            strStatus.c_str());

    m_getResponse->set(strStatus, entry, REDIS_ASIC_STATE_COMMAND_GETRESPONSE);

    SWSS_LOG_INFO("response for %s api was send",
            sai_serialize_common_api(api).c_str());
}

void Syncd::processFlexCounterGroupEvent( // TODO must be moved to go via ASIC channel queue
        _In_ swss::ConsumerTable& consumer)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(g_mutex); // TODO

    swss::KeyOpFieldsValuesTuple kco;

    consumer.pop(kco);

    auto& groupName = kfvKey(kco);
    auto& op = kfvOp(kco);
    auto& values = kfvFieldsValues(kco);

    if (op == SET_COMMAND)
    {
        m_manager->addCounterPlugin(groupName, values);
    }
    else if (op == DEL_COMMAND)
    {
        m_manager->removeCounterPlugins(groupName);
    }
    else
    {
        SWSS_LOG_ERROR("unknown command: %s", op.c_str());
    }
}

void Syncd::processFlexCounterEvent( // TODO must be moved to go via ASIC channel queue
        _In_ swss::ConsumerTable& consumer)
{
    SWSS_LOG_ENTER();

    std::lock_guard<std::mutex> lock(g_mutex); // TODO

    swss::KeyOpFieldsValuesTuple kco;

    consumer.pop(kco);

    auto& key = kfvKey(kco);
    auto& op = kfvOp(kco);

    auto delimiter = key.find_first_of(":");

    if (delimiter == std::string::npos)
    {
        SWSS_LOG_ERROR("Failed to parse the key %s", key.c_str());

        return; // if key is invalid there is no need to process this event again
    }

    auto groupName = key.substr(0, delimiter);
    auto strVid = key.substr(delimiter + 1);

    sai_object_id_t vid;
    sai_deserialize_object_id(strVid, vid);

    sai_object_id_t rid;

    if (!g_translator->tryTranslateVidToRid(vid, rid))
    {
        SWSS_LOG_WARN("port VID %s, was not found (probably port was removed/splitted) and will remove from counters now",
                sai_serialize_object_id(vid).c_str());

        op = DEL_COMMAND;
    }

    const auto values = kfvFieldsValues(kco);

    if (op == SET_COMMAND)
    {
        m_manager->addCounter(vid, rid, groupName, values);
    }
    else if (op == DEL_COMMAND)
    {
        m_manager->removeCounter(vid, groupName);
    }
    else
    {
        SWSS_LOG_ERROR("unknown command: %s", op.c_str());
    }
}


