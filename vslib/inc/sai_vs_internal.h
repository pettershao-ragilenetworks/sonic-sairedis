
#define MUTEX() std::lock_guard<std::recursive_mutex> _lock(saivs::Globals::apimutex)

#define VS_CHECK_API_INITIALIZED()                                          \
    if (!saivs::Globals::apiInitialized) {                                  \
        SWSS_LOG_ERROR("%s: api not initialized", __PRETTY_FUNCTION__);     \
        return SAI_STATUS_FAILURE; }

// object id

#define VS_CREATE(OBJECT_TYPE,object_type)                          \
    sai_status_t vs_create_ ## object_type(                         \
            _Out_ sai_object_id_t *object_type ##_id,               \
            _In_ sai_object_id_t switch_id,                         \
            _In_ uint32_t attr_count,                               \
            _In_ const sai_attribute_t *attr_list)                  \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return meta_sai_create_oid(                                 \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                switch_id,                                          \
                attr_count,                                         \
                attr_list,                                          \
                &vs_generic_create);                                \
    }

#define VS_REMOVE(OBJECT_TYPE,object_type)                          \
    sai_status_t vs_remove_ ## object_type(                         \
            _In_ sai_object_id_t object_type ## _id)                \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return meta_sai_remove_oid(                                 \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                &vs_generic_remove);                                \
    }

#define VS_SET(OBJECT_TYPE,object_type)                             \
    sai_status_t vs_set_ ## object_type ## _attribute(              \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ const sai_attribute_t *attr)                       \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return meta_sai_set_oid(                                    \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                attr,                                               \
                &vs_generic_set);                                   \
    }

#define VS_GET(OBJECT_TYPE,object_type)                             \
    sai_status_t vs_get_ ## object_type ## _attribute(              \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ uint32_t attr_count,                               \
            _Inout_ sai_attribute_t *attr_list)                     \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return meta_sai_get_oid(                                    \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                attr_count,                                         \
                attr_list,                                          \
                &vs_generic_get);                                   \
    }

#define VS_GENERIC_QUAD(OT,ot)  \
    VS_CREATE(OT,ot);           \
    VS_REMOVE(OT,ot);           \
    VS_SET(OT,ot);              \
    VS_GET(OT,ot);

// struct object id

#define VS_CREATE_ENTRY(OBJECT_TYPE,object_type)                \
    sai_status_t vs_create_ ## object_type(                     \
            _In_ const sai_ ## object_type ##_t *object_type,   \
            _In_ uint32_t attr_count,                           \
            _In_ const sai_attribute_t *attr_list)              \
    {                                                           \
        MUTEX();                                                \
        SWSS_LOG_ENTER();                                       \
        return meta_sai_create_ ## object_type(                 \
                object_type,                                    \
                attr_count,                                     \
                attr_list,                                      \
                &vs_generic_create_ ## object_type);            \
    }

#define VS_REMOVE_ENTRY(OBJECT_TYPE,object_type)                \
    sai_status_t vs_remove_ ## object_type(                     \
            _In_ const sai_ ## object_type ## _t *object_type)  \
    {                                                           \
        MUTEX();                                                \
        SWSS_LOG_ENTER();                                       \
        return meta_sai_remove_ ## object_type(                 \
                object_type,                                    \
                &vs_generic_remove_ ## object_type);            \
    }

#define VS_SET_ENTRY(OBJECT_TYPE,object_type)                   \
    sai_status_t vs_set_ ## object_type ## _attribute(          \
            _In_ const sai_ ## object_type ## _t *object_type,  \
            _In_ const sai_attribute_t *attr)                   \
    {                                                           \
        MUTEX();                                                \
        SWSS_LOG_ENTER();                                       \
        return meta_sai_set_ ## object_type(                    \
                object_type,                                    \
                attr,                                           \
                &vs_generic_set_ ## object_type);               \
    }

#define VS_GET_ENTRY(OBJECT_TYPE,object_type)                   \
    sai_status_t vs_get_ ## object_type ## _attribute(          \
            _In_ const sai_ ## object_type ## _t *object_type,  \
            _In_ uint32_t attr_count,                           \
            _Inout_ sai_attribute_t *attr_list)                 \
    {                                                           \
        MUTEX();                                                \
        SWSS_LOG_ENTER();                                       \
        return meta_sai_get_ ## object_type(                    \
                object_type,                                    \
                attr_count,                                     \
                attr_list,                                      \
                &vs_generic_get_ ## object_type);               \
    }

#define VS_GENERIC_QUAD_ENTRY(OT,ot)  \
    VS_CREATE_ENTRY(OT,ot);           \
    VS_REMOVE_ENTRY(OT,ot);           \
    VS_SET_ENTRY(OT,ot);              \
    VS_GET_ENTRY(OT,ot);

// common api

#define VS_GENERIC_QUAD_API(ot)     \
    vs_create_ ## ot,               \
    vs_remove_ ## ot,               \
    vs_set_ ## ot ##_attribute,     \
    vs_get_ ## ot ##_attribute,

// stats

#define VS_GET_STATS(OBJECT_TYPE,object_type)                       \
    sai_status_t vs_get_ ## object_type ## _stats(                  \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ uint32_t number_of_counters,                       \
            _In_ const sai_stat_id_t *counter_ids,                  \
            _Out_ uint64_t *counters)                               \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_vs->getStats(                                      \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                number_of_counters,                                 \
                counter_ids,                                        \
                counters);                                          \
    }

#define VS_GET_STATS_EXT(OBJECT_TYPE,object_type)                   \
    sai_status_t vs_get_ ## object_type ## _stats_ext(              \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ uint32_t number_of_counters,                       \
            _In_ const sai_stat_id_t *counter_ids,                  \
            _In_ sai_stats_mode_t mode,                             \
            _Out_ uint64_t *counters)                               \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_vs->getStatsExt(                                   \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                number_of_counters,                                 \
                counter_ids,                                        \
                mode,                                               \
                counters);                                          \
    }

#define VS_CLEAR_STATS(OBJECT_TYPE,object_type)                     \
    sai_status_t vs_clear_ ## object_type ## _stats(                \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ uint32_t number_of_counters,                       \
            _In_ const sai_stat_id_t *counter_ids)                  \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_vs->clearStats(                                    \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                number_of_counters,                                 \
                counter_ids);                                       \
    }

#define VS_GENERIC_STATS(OT, ot)    \
    VS_GET_STATS(OT,ot);            \
    VS_GET_STATS_EXT(OT,ot);        \
    VS_CLEAR_STATS(OT,ot);

// common stats api

#define VS_GENERIC_STATS_API(ot)    \
    vs_get_ ## ot ## _stats,        \
    vs_get_ ## ot ## _stats_ext,    \
    vs_clear_ ## ot ## _stats,

// BULK OID

#define VS_BULK_CREATE(OT,fname)                    \
static sai_status_t vs_bulk_create_ ## fname(       \
        _In_ sai_object_id_t switch_id,             \
        _In_ uint32_t object_count,                 \
        _In_ const uint32_t *attr_count,            \
        _In_ const sai_attribute_t **attr_list,     \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_object_id_t *object_id,           \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    VS_CHECK_API_INITIALIZED();                     \
    return g_meta->bulkCreate(                      \
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
static sai_status_t vs_bulk_remove_ ## fname(       \
        _In_ uint32_t object_count,                 \
        _In_ const sai_object_id_t *object_id,      \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    VS_CHECK_API_INITIALIZED();                     \
    return g_meta->bulkRemove(                      \
            SAI_OBJECT_TYPE_ ## OT,                 \
            object_count,                           \
            object_id,                              \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_SET(OT,fname)                       \
static sai_status_t vs_bulk_set_ ## fname(          \
        _In_ uint32_t object_count,                 \
        _In_ const sai_object_id_t *object_id,      \
        _In_ const sai_attribute_t *attr_list,      \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    VS_CHECK_API_INITIALIZED();                     \
    return g_meta->bulkSet(                         \
            SAI_OBJECT_TYPE_ ## OT,                 \
            object_count,                           \
            object_id,                              \
            attr_list,                              \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_GET(OT,fname)                       \
static sai_status_t vs_bulk_get_ ## fname(          \
        _In_ uint32_t object_count,                 \
        _In_ const sai_object_id_t *object_id,      \
        _In_ const uint32_t *attr_count,            \
        _Inout_ sai_attribute_t **attr_list,        \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    VS_CHECK_API_INITIALIZED();                     \
    SWSS_LOG_ERROR("not implemented");              \
    return SAI_STATUS_NOT_IMPLEMENTED;              \
}

#define VS_BULK_CREATE_ENTRY(OT,ot)                 \
static sai_status_t vs_bulk_create_ ## ot(          \
        _In_ uint32_t object_count,                 \
        _In_ const sai_ ## ot ## _t *entry,         \
        _In_ const uint32_t *attr_count,            \
        _In_ const sai_attribute_t **attr_list,     \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    VS_CHECK_API_INITIALIZED();                     \
    return g_meta->bulkCreate(                      \
            object_count,                           \
            entry,                                  \
            attr_count,                             \
            attr_list,                              \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_REMOVE_ENTRY(OT,ot)                 \
static sai_status_t vs_bulk_remove_ ## ot(          \
        _In_ uint32_t object_count,                 \
        _In_ const sai_ ## ot ##_t *entry,          \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    VS_CHECK_API_INITIALIZED();                     \
    return g_meta->bulkRemove(                      \
            object_count,                           \
            entry,                                  \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_SET_ENTRY(OT,ot)                    \
static sai_status_t vs_bulk_set_ ## ot(             \
        _In_ uint32_t object_count,                 \
        _In_ const sai_ ## ot ## _t *entry,         \
        _In_ const sai_attribute_t *attr_list,      \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    VS_CHECK_API_INITIALIZED();                     \
    return g_meta->bulkSet(                         \
            object_count,                           \
            entry,                                  \
            attr_list,                              \
            mode,                                   \
            object_statuses);                       \
}

#define VS_BULK_GET_ENTRY(OT,ot)                    \
static sai_status_t vs_bulk_get_ ## ot(             \
        _In_ uint32_t object_count,                 \
        _In_ const sai_ ## ot ## _t *entry,         \
        _In_ const uint32_t *attr_count,            \
        _Inout_ sai_attribute_t **attr_list,        \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    VS_CHECK_API_INITIALIZED();                     \
    SWSS_LOG_ERROR("not implemented");              \
    return SAI_STATUS_NOT_IMPLEMENTED;              \
}

#define VS_BULK_QUAD_ENTRY(OT,ot)    \
    VS_BULK_CREATE_ENTRY(OT,ot);     \
    VS_BULK_REMOVE_ENTRY(OT,ot);     \
    VS_BULK_SET_ENTRY(OT,ot);        \
    VS_BULK_GET_ENTRY(OT,ot);

#define VS_BULK_QUAD_API(ot)         \
    vs_bulk_create_ ## ot,           \
    vs_bulk_remove_ ## ot,           \
    vs_bulk_set_ ## ot,              \
    vs_bulk_get_ ## ot,

