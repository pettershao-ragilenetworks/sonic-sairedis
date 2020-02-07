#include "Syncd.h"
#include "lib/inc/sairediscommon.h"

#include "swss/logger.h"

#include "meta/sai_serialize.h"

#include <iterator>
#include <algorithm>

#include "syncd.h" // TODO to be removed

sai_status_t notifySyncd(
        _In_ const std::string& op);

sai_status_t processQuad(
        _In_ sai_common_api_t api,
        _In_ const swss::KeyOpFieldsValuesTuple &kco);

sai_status_t processBulkEvent(
        _In_ sai_common_api_t api,
        _In_ const swss::KeyOpFieldsValuesTuple &kco);

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

    std::lock_guard<std::mutex> lock(g_mutex);

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
        return processQuad(SAI_COMMON_API_CREATE, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_REMOVE)
        return processQuad(SAI_COMMON_API_REMOVE, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_SET)
        return processQuad(SAI_COMMON_API_SET, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_GET)
        return processQuad(SAI_COMMON_API_GET, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_BULK_CREATE)
        return processBulkEvent(SAI_COMMON_API_BULK_CREATE, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_BULK_REMOVE)
        return processBulkEvent(SAI_COMMON_API_BULK_REMOVE, kco);

    if (op == REDIS_ASIC_STATE_COMMAND_BULK_SET)
        return processBulkEvent(SAI_COMMON_API_BULK_SET, kco);

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

    sai_object_type_t object_type;
    sai_deserialize_object_type(fvValue(values[0]), object_type);

    sai_attr_id_t attr_id;
    sai_deserialize_attr_id(fvValue(values[1]), attr_id);

    uint32_t list_size = std::stoi(fvValue(values[2]));

    std::vector<int32_t> enum_capabilities_list(list_size);

    sai_s32_list_t enumCapList;

    enumCapList.count = list_size;
    enumCapList.list = enum_capabilities_list.data();

    sai_status_t status = g_vendorSai->queryAattributeEnumValuesCapability(switchRid, object_type, attr_id, &enumCapList);

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

    sai_object_type_t object_type;
    sai_deserialize_object_type(fvValue(values.back()), object_type);

    values.pop_back();

    SaiAttributeList list(object_type, values, false);

    sai_attribute_t *attr_list = list.get_attr_list();

    uint32_t attr_count = list.get_attr_count();

    g_translator->translateVidToRid(object_type, attr_count, attr_list);

    uint64_t count;

    sai_status_t status = g_vendorSai->objectTypeGetAvailability(
            switchRid,
            object_type,
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

template <typename T, typename G>
std::vector<T> extractCounterIdsGeneric(
        _In_ const swss::KeyOpFieldsValuesTuple &kco,
        _In_ G deserializeIdFn)
{
    SWSS_LOG_ENTER();

    const auto& values = kfvFieldsValues(kco);
    std::vector<T> counterIdList;
    for (const auto & v : values)
    {
        std::string field = fvField(v);
        T counterId;
        deserializeIdFn(field.c_str(), &counterId);

        counterIdList.push_back(counterId);
    }

    return counterIdList;
}

template <typename T, typename G>
sai_status_t clearStatsGeneric(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t object_id,
        _In_ const swss::KeyOpFieldsValuesTuple &kco,
        _In_ G deserializeIdFn)
{
    SWSS_LOG_ENTER();

    const std::vector<T> counter_ids = extractCounterIdsGeneric<T>(
            kco,
            deserializeIdFn);

    return g_vendorSai->clearStats(
            object_type,
            object_id,
            static_cast<uint32_t>(counter_ids.size()),
            reinterpret_cast<const sai_stat_id_t *>(counter_ids.data()));
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
