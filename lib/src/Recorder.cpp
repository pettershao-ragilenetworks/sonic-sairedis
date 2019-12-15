#include "swss/table.h"
#include <vector>
#include <fstream>

#include "Recorder.h"

#include "meta/sai_serialize.h"

#include <unistd.h>

#include <cstring>

using namespace sairedis;

std::string joinFieldValues(
        _In_ const std::vector<swss::FieldValueTuple> &values);

#define MUTEX() std::lock_guard<std::mutex> _lock(m_mutex)

Recorder::Recorder()
{
    SWSS_LOG_ENTER();

    m_recordingFileName = "sairedis.rec";

    m_recordingOutputDirectory = ".";

    m_performLogRotate = false;

    m_enabled = false;
}

Recorder::~Recorder()
{
    SWSS_LOG_ENTER();

    stopRecording();
}

bool Recorder::setRecordingOutputDirectory(
        _In_ const sai_attribute_t &attr)
{
    SWSS_LOG_ENTER();

    if (attr.value.s8list.count == 0)
    {
        m_recordingOutputDirectory = ".";

        SWSS_LOG_NOTICE("setting recording directory to: %s", m_recordingOutputDirectory.c_str());

        requestLogRotate();

        return true;
    }

    if (attr.value.s8list.list == NULL)
    {
        SWSS_LOG_ERROR("list pointer is NULL");

        return false;
    }

    size_t len = strnlen((const char *)attr.value.s8list.list, attr.value.s8list.count);

    if (len != (size_t)attr.value.s8list.count)
    {
        SWSS_LOG_ERROR("count (%u) is different than strnlen (%zu)", attr.value.s8list.count, len);

        return false;
    }

    std::string dir((const char*)attr.value.s8list.list, len);

    int result = access(dir.c_str(), W_OK);

    if (result != 0)
    {
        SWSS_LOG_ERROR("can't access dir '%s' for writing", dir.c_str());

        return false;
    }

    m_recordingOutputDirectory = dir;

    // perform log rotate when log directory gets changed

    requestLogRotate();

    return true;
}

void Recorder::enableRecording(
        _In_ bool enabled)
{
    SWSS_LOG_ENTER();

    m_enabled = enabled;

    stopRecording();

    if (enabled)
    {
        startRecording();
    }
}

void Recorder::recordLine(
        _In_ const std::string& line)
{
    MUTEX();

    SWSS_LOG_ENTER();

    if (!m_enabled)
    {
        return;
    }

    if (m_ofstream.is_open())
    {
        m_ofstream << getTimestamp() << "|" << line << std::endl;
    }

    if (m_performLogRotate)
    {
        m_performLogRotate = false;

        recordingFileReopen();

        /* double check since reopen could fail */

        if (m_ofstream.is_open())
        {
            m_ofstream << getTimestamp() << "|" << "#|logrotate on: " << m_recordingFile << std::endl;
        }
    }
}

void Recorder::requestLogRotate()
{
    SWSS_LOG_ENTER();

    m_performLogRotate = true;
}

void Recorder::recordingFileReopen()
{
    SWSS_LOG_ENTER();

    m_ofstream.close();

    /*
     * On log rotate we will use the same file name, we are assuming that
     * logrotate daemon move filename to filename.1 and we will create new
     * empty file here.
     */

    m_ofstream.open(m_recordingFile, std::ofstream::out | std::ofstream::app);

    if (!m_ofstream.is_open())
    {
        SWSS_LOG_ERROR("failed to open recording file %s: %s", m_recordingFile.c_str(), strerror(errno));
        return;
    }
}

void Recorder::startRecording()
{
    SWSS_LOG_ENTER();

    m_recordingFile = m_recordingOutputDirectory + "/" + m_recordingFileName;

    m_ofstream.open(m_recordingFile, std::ofstream::out | std::ofstream::app);

    if (!m_ofstream.is_open())
    {
        SWSS_LOG_ERROR("failed to open recording file %s: %s", m_recordingFile.c_str(), strerror(errno));
        return;
    }

    recordLine("#|recording on: " + m_recordingFile);

    SWSS_LOG_NOTICE("started recording: %s", m_recordingFileName.c_str());
}

void Recorder::stopRecording()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("stopped recording");

    if (m_ofstream.is_open())
    {
        m_ofstream.close();

        SWSS_LOG_NOTICE("closed recording file: %s", m_recordingFileName.c_str());
    }
}

std::string Recorder::getTimestamp()
{
    SWSS_LOG_ENTER();

    char buffer[64];
    struct timeval tv;

    gettimeofday(&tv, NULL);

    size_t size = strftime(buffer, 32 ,"%Y-%m-%d.%T.", localtime(&tv.tv_sec));

    snprintf(&buffer[size], 32, "%06ld", tv.tv_usec);

    return std::string(buffer);
}

// SAI APIs record functions

void Recorder::recordFlushFdbEntries(
        _In_ const std::string& key,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    recordLine("f|" + key + "|" + joinFieldValues(arguments));
}

void Recorder::recordFlushFdbEntriesResponse(
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    recordLine("F|" + sai_serialize_status(status));
}

void Recorder::recordQueryAttributeEnumValuesCapability(
        _In_ const std::string& key,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    recordLine("q|attribute_enum_values_capability|" + key + "|" + joinFieldValues(arguments));
}

void Recorder::recordQueryAttributeEnumValuesCapabilityResponse(
        _In_ sai_status_t status, 
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    recordLine("Q|attribute_enum_values_capability|" + sai_serialize_status(status) + "|" + joinFieldValues(arguments));
}

void Recorder::recordNotifySyncd(
        _In_ const std::string& key)
{
    SWSS_LOG_ENTER();

    // lower case 'a' stands for notify syncd request

    recordLine("a|" + key);
}

void Recorder::recordNotifySyncdResponse(
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    // capital 'A' stands for notify syncd response

    recordLine("A|" + sai_serialize_status(status));
}

void Recorder::recordGenericCreate(
        _In_ const std::string& key,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    // lower case 'c' stands for create api

    recordLine("c|" + key + "|" + joinFieldValues(arguments));
}

void Recorder::recordGenericCreateResponse(
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    // TODO currently empty since used in async mode, but we should log this in
    // synchronous mode, and we could use "G" from GET api as response
}

void Recorder::recordBulkGenericCreate(
        _In_ const std::string& objectType,
        _In_ const std::vector<swss::FieldValueTuple>& entriesWithStatus)
{
    SWSS_LOG_ENTER();

    std::string joined;

    for (const auto &e: entriesWithStatus)
    {
        // ||obj_id|attr=val|attr=val|status||obj_id|attr=val|attr=val|status

        joined += "||" + fvField(e) + "|" + fvValue(e);
    }

    // capital 'C' stands for bulk CREATE operation.

    recordLine("C|" + objectType + joined);
}

void Recorder::recordBulkGenericCreateResponse(
        _In_ sai_status_t status,
        _In_ uint32_t objectCount,
        _In_ const sai_status_t *objectStatuses)
{
    SWSS_LOG_ENTER();

    // TODO currently empty since used in async mode, but we should log this in
    // synchronous mode, and we could use "G" from GET api as response
}

void Recorder::recordGenericRemove(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    auto key = sai_serialize_object_type(objectType) + ":" + sai_serialize_object_id(objectId);

    // lower case 'r' stands for REMOVE api
    recordLine("r|" + key);
}

void Recorder::recordGenericRemove(
        _In_ const std::string& key)
{
    SWSS_LOG_ENTER();

    // lower case 'r' stands for REMOVE api
    recordLine("r|" + key);
}

void Recorder::recordGenericRemoveResponse(
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    // TODO currently empty since used in async mode, but we should log this in
    // synchronous mode, and we could use "G" from GET api as response
}

void Recorder::recordBulkGenericRemove(
        _In_ const std::string& objectType,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    std::string joined;

    // TODO revisit

    for (const auto &e: arguments)
    {
        // ||obj_id||obj_id||...

        joined += "||" + fvField(e);
    }

    // capital 'R' stands for bulk REMOVE operation.

    recordLine("R|" + objectType + joined);
}

void Recorder::recordBulkGenericRemoveResponse(
        _In_ sai_status_t status,
        _In_ uint32_t objectCount,
        _In_ const sai_status_t *objectStatuses)
{
    SWSS_LOG_ENTER();

    // TODO currently empty since used in async mode, but we should log this in
    // synchronous mode, and we could use "G" from GET api as response
}

void Recorder::recordGenericSet(
        _In_ const std::string& key,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    // lower case 's' stands for SET api

    recordLine("s|" + key + "|" + joinFieldValues(arguments));
}

void Recorder::recordGenericSetResponse(
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    // TODO currently empty since used in async mode, but we should log this in
    // synchronous mode, and we could use "G" from GET api as response
}

void Recorder::recordBulkGenericSet(
        _In_ const std::string& key,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    std::string joined;

    for (const auto &e: arguments)
    {
        // ||obj_id|attr=val|status||obj_id|attr=val|status

        joined += "||" + fvField(e) + "|" + fvValue(e);
    }

    // capital 'S' stands for bulk SET operation.

    recordLine("S|" + key + joined);
}


void Recorder::recordBulkGenericSetResponse(
        _In_ sai_status_t status,
        _In_ uint32_t objectCount,
        _In_ const sai_status_t *objectStatuses)
{
    SWSS_LOG_ENTER();

    // TODO currently empty since used in async mode, but we should log this in
    // synchronous mode, and we could use "G" from GET api as response
}

void Recorder::recordGenericGet(
        _In_ const std::string& key,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    // lower case 'g' stands for GET api

    recordLine("g|" + key + "|" + joinFieldValues(arguments));
}

void Recorder::recordGenericGetResponse(
        _In_ sai_status_t status,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    // capital 'G' stands for GET api response

    recordLine("G|" + sai_serialize_status(status) + "|" + joinFieldValues(arguments));
}

void Recorder::recordGenericGetStats(
        _In_ const std::string& key,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    recordLine("q|get_stats|" + key + "|" + joinFieldValues(arguments));
}

void Recorder::recordGenericGetStatsResponse(
        _In_ sai_status_t status,
        _In_ uint32_t count,
        _In_ const uint64_t *counters)
{
    SWSS_LOG_ENTER();

    std::string joined;

    for (uint32_t idx = 0; idx < count; idx ++)
    {
        joined += "|" + std::to_string(counters[idx]);
    }

    recordLine("Q|get_stats|" + sai_serialize_status(status) + joined);
}

void Recorder::recordGenericClearStats(
        _In_ const std::string& key,
        _In_ const std::vector<swss::FieldValueTuple>& arguments)
{
    SWSS_LOG_ENTER();

    recordLine("q|clear_stats|" + key + "|" + joinFieldValues(arguments));
}

void Recorder::recordGenericClearStatsResponse(
        _In_ sai_status_t status)
{
    SWSS_LOG_ENTER();

    recordLine("Q|clear_stats|" + sai_serialize_status(status)); 
}

void Recorder::recordNotification(
        _In_ const std::string &name,
        _In_ const std::string &serializedNotification,
        _In_ const std::vector<swss::FieldValueTuple> &values)
{
    SWSS_LOG_ENTER();

    recordLine("n|" + name + "|" + serializedNotification + "|" + joinFieldValues(values));
}
