#pragma once

#include "RemoteSaiInterface.h"

#include <memory>

#define SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(ot) \
    virtual sai_status_t remove(                                    \
            _In_ const sai_ ## ot ## _t* ot) override;

#define SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(ot) \
    virtual sai_status_t create(                                    \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ uint32_t attr_count,                               \
            _In_ const sai_attribute_t *attr_list) override;

#define SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(ot)    \
    virtual sai_status_t set(                                       \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ const sai_attribute_t *attr) override;

#define SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(ot)    \
    virtual sai_status_t get(                                       \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ uint32_t attr_count,                               \
            _Out_ sai_attribute_t *attr_list) override;

#define SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_CREATE_ENTRY(ot)    \
    virtual sai_status_t bulkCreate(                                        \
            _In_ uint32_t object_count,                                     \
            _In_ const sai_ ## ot ## _t *ot,                                \
            _In_ const uint32_t *attr_count,                                \
            _In_ const sai_attribute_t **attr_list,                         \
            _In_ sai_bulk_op_error_mode_t mode,                             \
            _Out_ sai_status_t *object_statuses) override;

#define SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_REMOVE_ENTRY(ot)    \
    virtual sai_status_t bulkRemove(                                        \
            _In_ uint32_t object_count,                                     \
            _In_ const sai_ ## ot ## _t *ot,                                \
            _In_ sai_bulk_op_error_mode_t mode,                             \
            _Out_ sai_status_t *object_statuses) override;

#define SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_SET_ENTRY(ot)       \
    virtual sai_status_t bulkSet(                                           \
            _In_ uint32_t object_count,                                     \
            _In_ const sai_ ## ot ## _t *ot,                                \
            _In_ const sai_attribute_t *attr_list,                          \
            _In_ sai_bulk_op_error_mode_t mode,                             \
            _Out_ sai_status_t *object_statuses) override;

namespace sairedis
{
    /**
     * @brief Wrapper remote SAI interface.
     *
     * Class will wrap actual SAI implementation and it will provide recording
     * and other operations required on top of actual SAI interface
     * implementation.
     * 
     * At this points implementation can be done in any way.
     *
     * Wrapper should be used as metadata argument to make sure that all
     * arguments passed to wrapper are valid and correct.
     */
    class WrapperRemoteSaiInterface:
        public RemoteSaiInterface
    {
        public:

            WrapperRemoteSaiInterface(
                    _In_ std::shared_ptr<RemoteSaiInterface> impl);

            virtual ~WrapperRemoteSaiInterface() = default;

        public: // SAI interface overrides

            virtual sai_status_t create(
                    _In_ sai_object_type_t objectType,
                    _Out_ sai_object_id_t* objectId,
                    _In_ sai_object_id_t switchId,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list) override;

            virtual sai_status_t remove(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId) override;

            virtual sai_status_t set(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId,
                    _In_ const sai_attribute_t *attr) override;

            virtual sai_status_t get(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId,
                    _In_ uint32_t attr_count,
                    _Inout_ sai_attribute_t *attr_list) override;

        public: // create ENTRY

            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(inseg_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(ipmc_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(l2mc_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(mcast_fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(neighbor_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(route_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_CREATE_ENTRY(nat_entry);

        public: // remove ENTRY

            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(inseg_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(ipmc_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(l2mc_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(mcast_fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(neighbor_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(route_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_REMOVE_ENTRY(nat_entry);

        public: // set ENTRY

            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(inseg_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(ipmc_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(l2mc_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(mcast_fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(neighbor_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(route_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_SET_ENTRY(nat_entry);

        public: // get ENTRY

            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(inseg_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(ipmc_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(l2mc_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(mcast_fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(neighbor_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(route_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_GET_ENTRY(nat_entry);

        public: // bulk QUAD oid

            virtual sai_status_t bulkCreate(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t object_count,
                    _In_ const uint32_t *attr_count,
                    _In_ const sai_attribute_t **attr_list,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Out_ sai_object_id_t *object_id,
                    _Out_ sai_status_t *object_statuses) override;

            virtual sai_status_t bulkRemove(
                    _In_ sai_object_type_t object_type,
                    _In_ uint32_t object_count,
                    _In_ const sai_object_id_t *object_id,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Out_ sai_status_t *object_statuses) override;

            virtual sai_status_t bulkSet(
                    _In_ sai_object_type_t object_type,
                    _In_ uint32_t object_count,
                    _In_ const sai_object_id_t *object_id,
                    _In_ const sai_attribute_t *attr_list,
                    _In_ sai_bulk_op_error_mode_t mode,
                    _Out_ sai_status_t *object_statuses) override;

        public: // bulk create ENTRY

            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_CREATE_ENTRY(fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_CREATE_ENTRY(nat_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_CREATE_ENTRY(route_entry);

        public: // bulk remove ENTRY

            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_REMOVE_ENTRY(fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_REMOVE_ENTRY(nat_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_REMOVE_ENTRY(route_entry);

        public: // bulk set ENTRY

            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_SET_ENTRY(fdb_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_SET_ENTRY(nat_entry);
            SAIREDIS_WRAPPERREMOTESAIINTERFACE_DECLARE_BULK_SET_ENTRY(route_entry);

        public: // stats API

            virtual sai_status_t getStats(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _In_ uint32_t number_of_counters,
                    _In_ const sai_stat_id_t *counter_ids,
                    _Out_ uint64_t *counters) override;

            virtual sai_status_t getStatsExt(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _In_ uint32_t number_of_counters,
                    _In_ const sai_stat_id_t *counter_ids,
                    _In_ sai_stats_mode_t mode,
                    _Out_ uint64_t *counters) override;

            virtual sai_status_t clearStats(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _In_ uint32_t number_of_counters,
                    _In_ const sai_stat_id_t *counter_ids) override;

        public: // non QUAD API

            virtual sai_status_t flushFdbEntries(
                    _In_ sai_object_id_t switchId,
                    _In_ uint32_t attrCount,
                    _In_ const sai_attribute_t *attrList) override;

        public: // SAI API

            virtual sai_status_t objectTypeGetAvailability(
                    _In_ sai_object_id_t switchId,
                    _In_ sai_object_type_t objectType,
                    _In_ uint32_t attrCount,
                    _In_ const sai_attribute_t *attrList,
                    _Out_ uint64_t *count) override;

            virtual sai_status_t queryAattributeEnumValuesCapability(
                    _In_ sai_object_id_t switch_id,
                    _In_ sai_object_type_t object_type,
                    _In_ sai_attr_id_t attr_id,
                    _Inout_ sai_s32_list_t *enum_values_capability) override;

        private:

            std::shared_ptr<RemoteSaiInterface> m_implementation;
    };
}
