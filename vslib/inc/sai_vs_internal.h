
#define MUTEX() std::lock_guard<std::recursive_mutex> _lock(m_apimutex)

#define VS_CHECK_API_INITIALIZED()                                          \
    if (!m_apiInitialized) {                                                \
        SWSS_LOG_ERROR("%s: api not initialized", __PRETTY_FUNCTION__);     \
        return SAI_STATUS_FAILURE; }

// QUAD OID

#define VS_CREATE(OT,ot)                                \
    static sai_status_t vs_create_ ## ot(               \
            _Out_ sai_object_id_t *object_id,           \
            _In_ sai_object_id_t switch_id,             \
            _In_ uint32_t attr_count,                   \
            _In_ const sai_attribute_t *attr_list)      \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->create(                               \
            (sai_object_type_t)SAI_OBJECT_TYPE_ ## OT,  \
            object_id,                                  \
            switch_id,                                  \
            attr_count,                                 \
            attr_list);                                 \
}

#define VS_REMOVE(OT,ot)                                \
    static sai_status_t vs_remove_ ## ot(               \
            _In_ sai_object_id_t object_id)             \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->remove(                               \
            (sai_object_type_t)SAI_OBJECT_TYPE_ ## OT,  \
            object_id);                                 \
}

#define VS_SET(OT,ot)                                   \
    static sai_status_t vs_set_ ## ot ## _attribute(    \
            _In_ sai_object_id_t object_id,             \
            _In_ const sai_attribute_t *attr)           \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->set(                                  \
            (sai_object_type_t)SAI_OBJECT_TYPE_ ## OT,  \
            object_id,                                  \
            attr);                                      \
}

#define VS_GET(OT,ot)                                   \
    static sai_status_t vs_get_ ## ot ## _attribute(    \
            _In_ sai_object_id_t object_id,             \
            _In_ uint32_t attr_count,                   \
            _Inout_ sai_attribute_t *attr_list)         \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->get(                                  \
            (sai_object_type_t)SAI_OBJECT_TYPE_ ## OT,  \
            object_id,                                  \
            attr_count,                                 \
            attr_list);                                 \
}

// QUAD DECLARE

#define VS_GENERIC_QUAD(OT,ot)  \
    VS_CREATE(OT,ot);           \
    VS_REMOVE(OT,ot);           \
    VS_SET(OT,ot);              \
    VS_GET(OT,ot);

// QUAD ENTRY

#define VS_CREATE_ENTRY(OT,ot)                          \
    static sai_status_t vs_create_ ## ot(               \
            _In_ const sai_ ## ot ##_t *entry,          \
            _In_ uint32_t attr_count,                   \
            _In_ const sai_attribute_t *attr_list)      \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->create(                               \
            entry,                                      \
            attr_count,                                 \
            attr_list);                                 \
}

#define VS_REMOVE_ENTRY(OT,ot)                          \
    static sai_status_t vs_remove_ ## ot(               \
            _In_ const sai_ ## ot ## _t *entry)         \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->remove(                               \
            entry);                                     \
}

#define VS_SET_ENTRY(OT,ot)                             \
    static sai_status_t vs_set_ ## ot ## _attribute(    \
            _In_ const sai_ ## ot ## _t *entry,         \
            _In_ const sai_attribute_t *attr)           \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->set(                                  \
            entry,                                      \
            attr);                                      \
}

#define VS_GET_ENTRY(OT,ot)                             \
    static sai_status_t vs_get_ ## ot ## _attribute(    \
            _In_ const sai_ ## ot ## _t *entry,         \
            _In_ uint32_t attr_count,                   \
            _Inout_ sai_attribute_t *attr_list)         \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->get(                                  \
            entry,                                      \
            attr_count,                                 \
            attr_list);                                 \
}

// QUAD ENTRY DECLARE

#define VS_GENERIC_QUAD_ENTRY(OT,ot)    \
    VS_CREATE_ENTRY(OT,ot);             \
    VS_REMOVE_ENTRY(OT,ot);             \
    VS_SET_ENTRY(OT,ot);                \
    VS_GET_ENTRY(OT,ot);

// QUAD API

#define VS_GENERIC_QUAD_API(ot)     \
    vs_create_ ## ot,               \
    vs_remove_ ## ot,               \
    vs_set_ ## ot ##_attribute,     \
    vs_get_ ## ot ##_attribute,

// STATS

#define VS_GET_STATS(OT,ot)                             \
    static sai_status_t vs_get_ ## ot ## _stats(        \
            _In_ sai_object_id_t object_id,             \
            _In_ uint32_t number_of_counters,           \
            _In_ const sai_stat_id_t *counter_ids,      \
            _Out_ uint64_t *counters)                   \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->getStats(                             \
            (sai_object_type_t)SAI_OBJECT_TYPE_ ## OT,  \
            object_id,                                  \
            number_of_counters,                         \
            counter_ids,                                \
            counters);                                  \
}

#define VS_GET_STATS_EXT(OT,ot)                         \
    static sai_status_t vs_get_ ## ot ## _stats_ext(    \
            _In_ sai_object_id_t object_id,             \
            _In_ uint32_t number_of_counters,           \
            _In_ const sai_stat_id_t *counter_ids,      \
            _In_ sai_stats_mode_t mode,                 \
            _Out_ uint64_t *counters)                   \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->getStatsExt(                          \
            (sai_object_type_t)SAI_OBJECT_TYPE_ ## OT,  \
            object_id,                                  \
            number_of_counters,                         \
            counter_ids,                                \
            mode,                                       \
            counters);                                  \
}

#define VS_CLEAR_STATS(OT,ot)                           \
    static sai_status_t vs_clear_ ## ot ## _stats(      \
            _In_ sai_object_id_t object_id,             \
            _In_ uint32_t number_of_counters,           \
            _In_ const sai_stat_id_t *counter_ids)      \
{                                                       \
    SWSS_LOG_ENTER();                                   \
    return g_sai->clearStats(                           \
            (sai_object_type_t)SAI_OBJECT_TYPE_ ## OT,  \
            object_id,                                  \
            number_of_counters,                         \
            counter_ids);                               \
}

// STATS DECLARE

#define VS_GENERIC_STATS(OT, ot)    \
    VS_GET_STATS(OT,ot);            \
    VS_GET_STATS_EXT(OT,ot);        \
    VS_CLEAR_STATS(OT,ot);

// STATS API

#define VS_GENERIC_STATS_API(ot)    \
    vs_get_ ## ot ## _stats,        \
    vs_get_ ## ot ## _stats_ext,    \
    vs_clear_ ## ot ## _stats,

// BULK QUAD

#define VS_BULK_CREATE(OT,fname)                    \
    static sai_status_t vs_bulk_create_ ## fname(   \
            _In_ sai_object_id_t switch_id,         \
            _In_ uint32_t object_count,             \
            _In_ const uint32_t *attr_count,        \
            _In_ const sai_attribute_t **attr_list, \
            _In_ sai_bulk_op_error_mode_t mode,     \
            _Out_ sai_object_id_t *object_id,       \
            _Out_ sai_status_t *object_statuses)    \
{                                                   \
    SWSS_LOG_ENTER();                               \
    return g_sai->bulkCreate(                       \
            SAI_OBJECT_TYPE_ ## OT,                 \
            switch_id,                              \
            object_count,                           \
            attr_count,                             \
            attr_list,                              \
            mode,                                   \
            object_id,                              \
            object_statuses);                       \
}

#define VS_BULK_REMOVE(OT,fname)                    \
    static sai_status_t vs_bulk_remove_ ## fname(   \
            _In_ uint32_t object_count,             \
            _In_ const sai_object_id_t *object_id,  \
            _In_ sai_bulk_op_error_mode_t mode,     \
            _Out_ sai_status_t *object_statuses)    \
{                                                   \
    SWSS_LOG_ENTER();                               \
    return g_sai->bulkRemove(                       \
            SAI_OBJECT_TYPE_ ## OT,                 \
            object_count,                           \
            object_id,                              \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_SET(OT,fname)                       \
    static sai_status_t vs_bulk_set_ ## fname(      \
            _In_ uint32_t object_count,             \
            _In_ const sai_object_id_t *object_id,  \
            _In_ const sai_attribute_t *attr_list,  \
            _In_ sai_bulk_op_error_mode_t mode,     \
            _Out_ sai_status_t *object_statuses)    \
{                                                   \
    SWSS_LOG_ENTER();                               \
    return g_sai->bulkSet(                          \
            SAI_OBJECT_TYPE_ ## OT,                 \
            object_count,                           \
            object_id,                              \
            attr_list,                              \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_GET(OT,fname)                       \
    static sai_status_t vs_bulk_get_ ## fname(      \
            _In_ uint32_t object_count,             \
            _In_ const sai_object_id_t *object_id,  \
            _In_ const uint32_t *attr_count,        \
            _Inout_ sai_attribute_t **attr_list,    \
            _In_ sai_bulk_op_error_mode_t mode,     \
            _Out_ sai_status_t *object_statuses)    \
{                                                   \
    SWSS_LOG_ENTER();                               \
    SWSS_LOG_ERROR("not implemented");              \
    return SAI_STATUS_NOT_IMPLEMENTED;              \
}

// BULK QUAD DECLARE

#define VS_BULK_QUAD(OT,ot)     \
    VS_BULK_CREATE(OT,ot);      \
    VS_BULK_REMOVE(OT,ot);      \
    VS_BULK_SET(OT,ot);         \
    VS_BULK_GET(OT,ot);

// BULK QUAD ENTRY

#define VS_BULK_CREATE_ENTRY(OT,ot)                 \
    static sai_status_t vs_bulk_create_ ## ot(      \
            _In_ uint32_t object_count,             \
            _In_ const sai_ ## ot ## _t *entry,     \
            _In_ const uint32_t *attr_count,        \
            _In_ const sai_attribute_t **attr_list, \
            _In_ sai_bulk_op_error_mode_t mode,     \
            _Out_ sai_status_t *object_statuses)    \
{                                                   \
    SWSS_LOG_ENTER();                               \
    return g_sai->bulkCreate(                       \
            object_count,                           \
            entry,                                  \
            attr_count,                             \
            attr_list,                              \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_REMOVE_ENTRY(OT,ot)                 \
    static sai_status_t vs_bulk_remove_ ## ot(      \
            _In_ uint32_t object_count,             \
            _In_ const sai_ ## ot ##_t *entry,      \
            _In_ sai_bulk_op_error_mode_t mode,     \
            _Out_ sai_status_t *object_statuses)    \
{                                                   \
    SWSS_LOG_ENTER();                               \
    return g_sai->bulkRemove(                       \
            object_count,                           \
            entry,                                  \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_SET_ENTRY(OT,ot)                    \
    static sai_status_t vs_bulk_set_ ## ot(         \
            _In_ uint32_t object_count,             \
            _In_ const sai_ ## ot ## _t *entry,     \
            _In_ const sai_attribute_t *attr_list,  \
            _In_ sai_bulk_op_error_mode_t mode,     \
            _Out_ sai_status_t *object_statuses)    \
{                                                   \
    SWSS_LOG_ENTER();                               \
    return g_sai->bulkSet(                          \
            object_count,                           \
            entry,                                  \
            attr_list,                              \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_GET_ENTRY(OT,ot)                    \
    static sai_status_t vs_bulk_get_ ## ot(         \
            _In_ uint32_t object_count,             \
            _In_ const sai_ ## ot ## _t *entry,     \
            _In_ const uint32_t *attr_count,        \
            _Inout_ sai_attribute_t **attr_list,    \
            _In_ sai_bulk_op_error_mode_t mode,     \
            _Out_ sai_status_t *object_statuses)    \
{                                                   \
    SWSS_LOG_ENTER();                               \
    SWSS_LOG_ERROR("not implemented");              \
    return SAI_STATUS_NOT_IMPLEMENTED;              \
}

// BULK QUAD ENTRY DECLARE

#define VS_BULK_QUAD_ENTRY(OT,ot)   \
    VS_BULK_CREATE_ENTRY(OT,ot);    \
    VS_BULK_REMOVE_ENTRY(OT,ot);    \
    VS_BULK_SET_ENTRY(OT,ot);       \
    VS_BULK_GET_ENTRY(OT,ot);

// BULK QUAD API

#define VS_BULK_QUAD_API(ot)        \
    vs_bulk_create_ ## ot,          \
    vs_bulk_remove_ ## ot,          \
    vs_bulk_set_ ## ot,             \
    vs_bulk_get_ ## ot,

