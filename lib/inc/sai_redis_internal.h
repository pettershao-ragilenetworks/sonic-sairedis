
#define MUTEX() std::lock_guard<std::mutex> _lock(sairedis::Globals::apimutex)

// object id

#define REDIS_CREATE(OBJECT_TYPE,object_type)                       \
static sai_status_t redis_create_ ## object_type(                   \
            _Out_ sai_object_id_t *object_type ##_id,               \
            _In_ sai_object_id_t switch_id,                         \
            _In_ uint32_t attr_count,                               \
            _In_ const sai_attribute_t *attr_list)                  \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_meta->create(                                      \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                switch_id,                                          \
                attr_count,                                         \
                attr_list,                                          \
                *g_remoteSaiInterface);                             \
    }

#define REDIS_REMOVE(OBJECT_TYPE,object_type)                       \
static sai_status_t redis_remove_ ## object_type(                   \
            _In_ sai_object_id_t object_type ## _id)                \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_meta->remove(                                      \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                *g_remoteSaiInterface);                             \
    }

#define REDIS_SET(OBJECT_TYPE,object_type)                          \
static sai_status_t redis_set_ ##object_type ## _attribute(         \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ const sai_attribute_t *attr)                       \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_meta->set(                                         \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                attr,                                               \
                *g_remoteSaiInterface);                             \
    }

#define REDIS_GET(OBJECT_TYPE,object_type)                          \
static sai_status_t redis_get_ ##object_type ## _attribute(         \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ uint32_t attr_count,                               \
            _Inout_ sai_attribute_t *attr_list)                     \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_meta->get(                                         \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                attr_count,                                         \
                attr_list,                                          \
                *g_remoteSaiInterface);                             \
    }

#define REDIS_GENERIC_QUAD(OT,ot)  \
    REDIS_CREATE(OT,ot);           \
    REDIS_REMOVE(OT,ot);           \
    REDIS_SET(OT,ot);              \
    REDIS_GET(OT,ot);

// struct object id

#define REDIS_CREATE_ENTRY(OBJECT_TYPE,object_type)             \
static sai_status_t redis_create_ ## object_type(               \
            _In_ const sai_ ## object_type ##_t *object_type,   \
            _In_ uint32_t attr_count,                           \
            _In_ const sai_attribute_t *attr_list)              \
    {                                                           \
        MUTEX();                                                \
        SWSS_LOG_ENTER();                                       \
        return g_meta->create(                                  \
                object_type,                                    \
                attr_count,                                     \
                attr_list,                                      \
                *g_remoteSaiInterface);                         \
    }

#define REDIS_REMOVE_ENTRY(OBJECT_TYPE,object_type)             \
static sai_status_t redis_remove_ ## object_type(               \
            _In_ const sai_ ## object_type ## _t *object_type)  \
    {                                                           \
        MUTEX();                                                \
        SWSS_LOG_ENTER();                                       \
        return g_meta->remove(                                  \
                object_type,                                    \
                *g_remoteSaiInterface);                         \
    }

#define REDIS_SET_ENTRY(OBJECT_TYPE,object_type)                \
static sai_status_t redis_set_ ## object_type ## _attribute(    \
            _In_ const sai_ ## object_type ## _t *object_type,  \
            _In_ const sai_attribute_t *attr)                   \
    {                                                           \
        MUTEX();                                                \
        SWSS_LOG_ENTER();                                       \
        return g_meta->set(                                     \
                object_type,                                    \
                attr,                                           \
                 *g_remoteSaiInterface);                        \
    }

#define REDIS_GET_ENTRY(OBJECT_TYPE,object_type)                \
static sai_status_t redis_get_ ## object_type ## _attribute(    \
            _In_ const sai_ ## object_type ## _t *object_type,  \
            _In_ uint32_t attr_count,                           \
            _Inout_ sai_attribute_t *attr_list)                 \
    {                                                           \
        MUTEX();                                                \
        SWSS_LOG_ENTER();                                       \
        return g_meta->get(                                     \
                object_type,                                    \
                attr_count,                                     \
                attr_list,                                      \
                *g_remoteSaiInterface);                         \
    }

#define REDIS_GENERIC_QUAD_ENTRY(OT,ot)  \
    REDIS_CREATE_ENTRY(OT,ot);           \
    REDIS_REMOVE_ENTRY(OT,ot);           \
    REDIS_SET_ENTRY(OT,ot);              \
    REDIS_GET_ENTRY(OT,ot);

// common api

#define REDIS_GENERIC_QUAD_API(ot)     \
    redis_create_ ## ot,               \
    redis_remove_ ## ot,               \
    redis_set_ ## ot ##_attribute,     \
    redis_get_ ## ot ##_attribute,

// stats

#define REDIS_GET_STATS(OBJECT_TYPE,object_type)                    \
static sai_status_t redis_get_ ## object_type ## _stats(            \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ uint32_t number_of_counters,                       \
            _In_ const sai_stat_id_t *counter_ids,                  \
            _Out_ uint64_t *counters)                               \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_meta->getStats(                                    \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                number_of_counters,                                 \
                counter_ids,                                        \
                counters,                                           \
                *g_remoteSaiInterface);                             \
    }

#define REDIS_GET_STATS_EXT(OBJECT_TYPE,object_type)                \
static sai_status_t redis_get_ ## object_type ## _stats_ext(        \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ uint32_t number_of_counters,                       \
            _In_ const sai_stat_id_t *counter_ids,                  \
            _In_ sai_stats_mode_t mode,                             \
            _Out_ uint64_t *counters)                               \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_meta->getStatsExt(                                 \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                number_of_counters,                                 \
                counter_ids,                                        \
                mode,                                               \
                counters,                                           \
                *g_remoteSaiInterface);                             \
    }

#define REDIS_CLEAR_STATS(OBJECT_TYPE,object_type)                  \
static sai_status_t redis_clear_ ## object_type ## _stats(          \
            _In_ sai_object_id_t object_type ## _id,                \
            _In_ uint32_t number_of_counters,                       \
            _In_ const sai_stat_id_t *counter_ids)                  \
    {                                                               \
        MUTEX();                                                    \
        SWSS_LOG_ENTER();                                           \
        return g_meta->clearStats(                                  \
                (sai_object_type_t)SAI_OBJECT_TYPE_ ## OBJECT_TYPE, \
                object_type ## _id,                                 \
                number_of_counters,                                 \
                counter_ids,                                        \
                *g_remoteSaiInterface);                             \
    }

#define REDIS_GENERIC_STATS(OT, ot)    \
    REDIS_GET_STATS(OT,ot);            \
    REDIS_GET_STATS_EXT(OT,ot);        \
    REDIS_CLEAR_STATS(OT,ot);

// common stats api

#define REDIS_GENERIC_STATS_API(ot)    \
    redis_get_ ## ot ## _stats,        \
    redis_get_ ## ot ## _stats_ext,    \
    redis_clear_ ## ot ## _stats,

#define REDIS_DECLARE_EVERY_ENTRY(_X)       \
    _X(FDB_ENTRY,fdb_entry);                \
    _X(INSEG_ENTRY,inseg_entry);            \
    _X(IPMC_ENTRY,ipmc_entry);              \
    _X(L2MC_ENTRY,l2mc_entry);              \
    _X(MCAST_FDB_ENTRY,mcast_fdb_entry);    \
    _X(NEIGHBOR_ENTRY,neighbor_entry);      \
    _X(ROUTE_ENTRY,route_entry);            \
    _X(NAT_ENTRY,nat_entry);                \

// BULK OID

#define REDIS_BULK_CREATE(OT,fname)                 \
static sai_status_t redis_bulk_create_ ## fname(    \
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
    return redis_bulk_generic_create(               \
            SAI_OBJECT_TYPE_ ## OT,                 \
            switch_id,                              \
            object_count,                           \
            attr_count,                             \
            attr_list,                              \
            mode,                                   \
            object_id,                              \
            object_statuses);                       \
}

#define REDIS_BULK_REMOVE(OT,fname)                 \
static sai_status_t redis_bulk_remove_ ## fname(    \
        _In_ uint32_t object_count,                 \
        _In_ const sai_object_id_t *object_id,      \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    return g_meta->bulkRemove(                      \
            SAI_OBJECT_TYPE_ ## OT,                 \
            object_count,                           \
            object_id,                              \
            mode,                                   \
            object_statuses,                        \
            *g_remoteSaiInterface);                 \
}

#define REDIS_BULK_SET(OT,fname)                    \
static sai_status_t redis_bulk_set_ ## fname(       \
        _In_ uint32_t object_count,                 \
        _In_ const sai_object_id_t *object_id,      \
        _In_ const sai_attribute_t *attr_list,      \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    return g_meta->bulkSet(                         \
            SAI_OBJECT_TYPE_ ## OT,                 \
            object_count,                           \
            object_id,                              \
            attr_list,                              \
            mode,                                   \
            object_statuses,                        \
            *g_remoteSaiInterface);                 \
}

#define REDIS_BULK_GET(OT,fname)                    \
static sai_status_t redis_bulk_get_ ## fname(       \
        _In_ uint32_t object_count,                 \
        _In_ const sai_object_id_t *object_id,      \
        _In_ const uint32_t *attr_count,            \
        _Inout_ sai_attribute_t **attr_list,        \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    return redis_bulk_generic_get(a                 \
            SAI_OBJECT_TYPE_ ## OT,                 \
            object_count,                           \
            object_id,                              \
            attr_count,                             \
            attr_list,                              \
            mode,                                   \
            object_statuses);                       \
}

#define REDIS_BULK_CREATE_ENTRY(OT,ot)              \
static sai_status_t redis_bulk_create_ ## ot(       \
        _In_ uint32_t object_count,                 \
        _In_ const sai_ ## ot ## _t *entry,         \
        _In_ const uint32_t *attr_count,            \
        _In_ const sai_attribute_t **attr_list,     \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    return redis_bulk_create_entry(                 \
            SAI_OBJECT_TYPE_ ## OT,                 \
            object_count,                           \
            entry,                                  \
            attr_count,                             \
            attr_list,                              \
            mode,                                   \
            object_statuses);                       \
}

#define REDIS_BULK_REMOVE_ENTRY(OT,ot)              \
static sai_status_t redis_bulk_remove_ ## ot(       \
        _In_ uint32_t object_count,                 \
        _In_ const sai_ ## ot ##_t *entry,          \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    return g_meta->bulkRemove(                      \
            object_count,                           \
            entry,                                  \
            mode,                                   \
            object_statuses,                        \
            *g_remoteSaiInterface);                 \
}

#define REDIS_BULK_SET_ENTRY(OT,ot)                 \
static sai_status_t redis_bulk_set_ ## ot(          \
        _In_ uint32_t object_count,                 \
        _In_ const sai_ ## ot ## _t *entry,         \
        _In_ const sai_attribute_t *attr_list,      \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    return g_meta->bulkSet(                         \
            object_count,                           \
            entry,                                  \
            attr_list,                              \
            mode,                                   \
            object_statuses,                        \
            *g_remoteSaiInterface);                 \
}

#define REDIS_BULK_GET_ENTRY(OT,ot)                 \
static sai_status_t redis_bulk_get_ ## ot(          \
        _In_ uint32_t object_count,                 \
        _In_ const sai_ ## ot ## _t *entry,         \
        _In_ const uint32_t *attr_count,            \
        _Inout_ sai_attribute_t **attr_list,        \
        _In_ sai_bulk_op_error_mode_t mode,         \
        _Out_ sai_status_t *object_statuses)        \
{                                                   \
    MUTEX();                                        \
    SWSS_LOG_ENTER();                               \
    SWSS_LOG_ERROR("not implemented");              \
    return SAI_STATUS_NOT_IMPLEMENTED;              \
}

#define REDIS_BULK_QUAD_ENTRY(OT,ot)    \
    REDIS_BULK_CREATE_ENTRY(OT,ot);     \
    REDIS_BULK_REMOVE_ENTRY(OT,ot);     \
    REDIS_BULK_SET_ENTRY(OT,ot);        \
    REDIS_BULK_GET_ENTRY(OT,ot);

#define REDIS_BULK_QUAD_API(ot)         \
    redis_bulk_create_ ## ot,           \
    redis_bulk_remove_ ## ot,           \
    redis_bulk_set_ ## ot,              \
    redis_bulk_get_ ## ot,

