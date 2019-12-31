#pragma once

#include "lib/inc/SaiInterface.h"

#include "SaiAttrWrapper.h"
#include "SaiObjectCollection.h"
#include "PortRelatedSet.h"
#include "AttrKeyMap.h"
#include "OidRefCounter.h"

#include <vector>
#include <memory>
#include <set>

#define SAIREDIS_META_DECLARE_REMOVE_ENTRY(ot) \
    virtual sai_status_t remove(                                    \
            _In_ const sai_ ## ot ## _t* ot) override;

#define SAIREDIS_META_DECLARE_CREATE_ENTRY(ot) \
    virtual sai_status_t create(                                    \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ uint32_t attr_count,                               \
            _In_ const sai_attribute_t *attr_list) override;

#define SAIREDIS_META_DECLARE_SET_ENTRY(ot)    \
    virtual sai_status_t set(                                       \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ const sai_attribute_t *attr) override;

#define SAIREDIS_META_DECLARE_GET_ENTRY(ot)    \
    virtual sai_status_t get(                                       \
            _In_ const sai_ ## ot ## _t* ot,                        \
            _In_ uint32_t attr_count,                               \
            _Out_ sai_attribute_t *attr_list) override;

#define SAIREDIS_META_DECLARE_BULK_CREATE_ENTRY(ot)    \
    virtual sai_status_t bulkCreate(                                        \
            _In_ uint32_t object_count,                                     \
            _In_ const sai_ ## ot ## _t *ot,                                \
            _In_ const uint32_t *attr_count,                                \
            _In_ const sai_attribute_t **attr_list,                         \
            _In_ sai_bulk_op_error_mode_t mode,                             \
            _Out_ sai_status_t *object_statuses) override;

#define SAIREDIS_META_DECLARE_BULK_REMOVE_ENTRY(ot)    \
    virtual sai_status_t bulkRemove(                                        \
            _In_ uint32_t object_count,                                     \
            _In_ const sai_ ## ot ## _t *ot,                                \
            _In_ sai_bulk_op_error_mode_t mode,                             \
            _Out_ sai_status_t *object_statuses) override;

#define SAIREDIS_META_DECLARE_BULK_SET_ENTRY(ot)       \
    virtual sai_status_t bulkSet(                                           \
            _In_ uint32_t object_count,                                     \
            _In_ const sai_ ## ot ## _t *ot,                                \
            _In_ const sai_attribute_t *attr_list,                          \
            _In_ sai_bulk_op_error_mode_t mode,                             \
            _Out_ sai_status_t *object_statuses) override;

namespace saimeta
{
    class Meta:
        public sairedis::SaiInterface
    {
        public:

            Meta(
                    _In_ std::shared_ptr<SaiInterface> impl);

            virtual ~Meta() = default;

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

            SAIREDIS_META_DECLARE_CREATE_ENTRY(fdb_entry);
            SAIREDIS_META_DECLARE_CREATE_ENTRY(inseg_entry);
            SAIREDIS_META_DECLARE_CREATE_ENTRY(ipmc_entry);
            SAIREDIS_META_DECLARE_CREATE_ENTRY(l2mc_entry);
            SAIREDIS_META_DECLARE_CREATE_ENTRY(mcast_fdb_entry);
            SAIREDIS_META_DECLARE_CREATE_ENTRY(neighbor_entry);
            SAIREDIS_META_DECLARE_CREATE_ENTRY(route_entry);
            SAIREDIS_META_DECLARE_CREATE_ENTRY(nat_entry);

        public: // remove ENTRY

            SAIREDIS_META_DECLARE_REMOVE_ENTRY(fdb_entry);
            SAIREDIS_META_DECLARE_REMOVE_ENTRY(inseg_entry);
            SAIREDIS_META_DECLARE_REMOVE_ENTRY(ipmc_entry);
            SAIREDIS_META_DECLARE_REMOVE_ENTRY(l2mc_entry);
            SAIREDIS_META_DECLARE_REMOVE_ENTRY(mcast_fdb_entry);
            SAIREDIS_META_DECLARE_REMOVE_ENTRY(neighbor_entry);
            SAIREDIS_META_DECLARE_REMOVE_ENTRY(route_entry);
            SAIREDIS_META_DECLARE_REMOVE_ENTRY(nat_entry);

        public: // set ENTRY

            SAIREDIS_META_DECLARE_SET_ENTRY(fdb_entry);
            SAIREDIS_META_DECLARE_SET_ENTRY(inseg_entry);
            SAIREDIS_META_DECLARE_SET_ENTRY(ipmc_entry);
            SAIREDIS_META_DECLARE_SET_ENTRY(l2mc_entry);
            SAIREDIS_META_DECLARE_SET_ENTRY(mcast_fdb_entry);
            SAIREDIS_META_DECLARE_SET_ENTRY(neighbor_entry);
            SAIREDIS_META_DECLARE_SET_ENTRY(route_entry);
            SAIREDIS_META_DECLARE_SET_ENTRY(nat_entry);

        public: // get ENTRY

            SAIREDIS_META_DECLARE_GET_ENTRY(fdb_entry);
            SAIREDIS_META_DECLARE_GET_ENTRY(inseg_entry);
            SAIREDIS_META_DECLARE_GET_ENTRY(ipmc_entry);
            SAIREDIS_META_DECLARE_GET_ENTRY(l2mc_entry);
            SAIREDIS_META_DECLARE_GET_ENTRY(mcast_fdb_entry);
            SAIREDIS_META_DECLARE_GET_ENTRY(neighbor_entry);
            SAIREDIS_META_DECLARE_GET_ENTRY(route_entry);
            SAIREDIS_META_DECLARE_GET_ENTRY(nat_entry);

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

            SAIREDIS_META_DECLARE_BULK_CREATE_ENTRY(fdb_entry);
            SAIREDIS_META_DECLARE_BULK_CREATE_ENTRY(nat_entry);
            SAIREDIS_META_DECLARE_BULK_CREATE_ENTRY(route_entry);

        public: // bulk remove ENTRY

            SAIREDIS_META_DECLARE_BULK_REMOVE_ENTRY(fdb_entry);
            SAIREDIS_META_DECLARE_BULK_REMOVE_ENTRY(nat_entry);
            SAIREDIS_META_DECLARE_BULK_REMOVE_ENTRY(route_entry);

        public: // bulk set ENTRY

            SAIREDIS_META_DECLARE_BULK_SET_ENTRY(fdb_entry);
            SAIREDIS_META_DECLARE_BULK_SET_ENTRY(nat_entry);
            SAIREDIS_META_DECLARE_BULK_SET_ENTRY(route_entry);

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

            virtual sai_object_type_t objectTypeQuery(
                    _In_ sai_object_id_t objectId) override;

            virtual sai_object_id_t switchIdQuery(
                    _In_ sai_object_id_t objectId) override;

        public:

            void meta_init_db();

        public: // notifications

            void meta_sai_on_fdb_event(
                    _In_ uint32_t count,
                    _In_ sai_fdb_event_notification_data_t *data);

            void meta_sai_on_switch_state_change(
                    _In_ sai_object_id_t switch_id,
                    _In_ sai_switch_oper_status_t switch_oper_status);

            void meta_sai_on_switch_shutdown_request(
                    _In_ sai_object_id_t switch_id);

            void meta_sai_on_port_state_change(
                    _In_ uint32_t count,
                    _In_ const sai_port_oper_status_notification_t *data);

            void meta_sai_on_queue_pfc_deadlock_notification(
                    _In_ uint32_t count,
                    _In_ const sai_queue_deadlock_notification_data_t *data);

        private: // notifications helpers

            void meta_sai_on_fdb_flush_event_consolidated(
                    _In_ const sai_fdb_event_notification_data_t& data);

            void meta_fdb_event_snoop_oid(
                    _In_ sai_object_id_t oid);

            void meta_sai_on_fdb_event_single(
                    _In_ const sai_fdb_event_notification_data_t& data);

            void meta_sai_on_port_state_change_single(
                    _In_ const sai_port_oper_status_notification_t& data);

            void meta_sai_on_queue_pfc_deadlock_notification_single(
                    _In_ const sai_queue_deadlock_notification_data_t& data);

        private: // validation helpers

            sai_status_t meta_generic_validation_objlist(
                    _In_ const sai_attr_metadata_t& md,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t count,
                    _In_ const sai_object_id_t* list);

            sai_status_t meta_genetic_validation_list(
                    _In_ const sai_attr_metadata_t& md,
                    _In_ uint32_t count,
                    _In_ const void* list);

            sai_status_t meta_generic_validate_non_object_on_create(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ sai_object_id_t switch_id);

            sai_object_id_t meta_extract_switch_id(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ sai_object_id_t switch_id);

            std::shared_ptr<SaiAttrWrapper> get_object_previous_attr(
                    _In_ const sai_object_meta_key_t& metaKey,
                    _In_ const sai_attr_metadata_t& md);

            std::vector<const sai_attr_metadata_t*> get_attributes_metadata(
                    _In_ sai_object_type_t objecttype);

            void meta_generic_validation_post_get_objlist(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ const sai_attr_metadata_t& md,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t count,
                    _In_ const sai_object_id_t* list);

        public:

            static bool is_ipv6_mask_valid(
                    _In_ const uint8_t* mask);

        private: // unit tests helpers

            bool meta_unittests_get_and_erase_set_readonly_flag(
                    _In_ const sai_attr_metadata_t& md);

        public:

            void meta_unittests_enable(
                    _In_ bool enable);

            bool meta_unittests_enabled();

        public: // unittests method helpers

            int32_t getObjectReferenceCount(
                    _In_ sai_object_id_t oid) const;

            bool objectExists(
                    _In_ const std::string& mk) const;

        private: // port helpers

            sai_status_t meta_port_remove_validation(
                    _In_ const sai_object_meta_key_t& meta_key);

            bool meta_is_object_in_default_state(
                    _In_ sai_object_id_t oid);

            void post_port_remove(
                    _In_ const sai_object_meta_key_t& meta_key);

            void meta_post_port_get(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ sai_object_id_t switch_id,
                    _In_ const uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            void meta_add_port_to_related_map(
                    _In_ sai_object_id_t port_id,
                    _In_ const sai_object_list_t& list);

        private: // validation post QUAD

            void meta_generic_validation_post_create(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ sai_object_id_t switch_id,
                    _In_ const uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            void meta_generic_validation_post_remove(
                    _In_ const sai_object_meta_key_t& meta_key);

            void meta_generic_validation_post_set(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ const sai_attribute_t *attr);

            void meta_generic_validation_post_get(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ sai_object_id_t switch_id,
                    _In_ const uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

        private: // validation QUAD

            sai_status_t meta_generic_validation_create(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ sai_object_id_t switch_id,
                    _In_ const uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list);

            sai_status_t meta_generic_validation_remove(
                    _In_ const sai_object_meta_key_t& meta_key);

            sai_status_t meta_generic_validation_set(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ const sai_attribute_t *attr);

            sai_status_t meta_generic_validation_get(
                    _In_ const sai_object_meta_key_t& meta_key,
                    _In_ const uint32_t attr_count,
                    _In_ sai_attribute_t *attr_list);

        private: // stats

            sai_status_t meta_validate_stats(
                    _In_ sai_object_type_t object_type,
                    _In_ sai_object_id_t object_id,
                    _In_ uint32_t number_of_counters,
                    _In_ const sai_stat_id_t *counter_ids,
                    _Out_ uint64_t *counters,
                    _In_ sai_stats_mode_t mode);

        private: // validate OID

            sai_status_t meta_sai_validate_oid(
                    _In_ sai_object_type_t object_type,
                    _In_ const sai_object_id_t* object_id,
                    _In_ sai_object_id_t switch_id,
                    _In_ bool create);

        private: // validate ENTRY

            sai_status_t meta_sai_validate_fdb_entry(
                    _In_ const sai_fdb_entry_t* fdb_entry,
                    _In_ bool create,
                    _In_ bool get = false);

            sai_status_t meta_sai_validate_mcast_fdb_entry(
                    _In_ const sai_mcast_fdb_entry_t* mcast_fdb_entry,
                    _In_ bool create,
                    _In_ bool get = false);

            sai_status_t meta_sai_validate_neighbor_entry(
                    _In_ const sai_neighbor_entry_t* neighbor_entry,
                    _In_ bool create);

            sai_status_t meta_sai_validate_route_entry(
                    _In_ const sai_route_entry_t* route_entry,
                    _In_ bool create);

            sai_status_t meta_sai_validate_l2mc_entry(
                    _In_ const sai_l2mc_entry_t* l2mc_entry,
                    _In_ bool create);

            sai_status_t meta_sai_validate_ipmc_entry(
                    _In_ const sai_ipmc_entry_t* ipmc_entry,
                    _In_ bool create);

            sai_status_t meta_sai_validate_nat_entry(
                    _In_ const sai_nat_entry_t* nat_entry,
                    _In_ bool create);

            sai_status_t meta_sai_validate_inseg_entry(
                    _In_ const sai_inseg_entry_t* inseg_entry,
                    _In_ bool create);

        private:

            std::shared_ptr<sairedis::SaiInterface> m_implementation;

        private: // database objects

            PortRelatedSet m_portRelatedSet;

            OidRefCounter m_oids;

            SaiObjectCollection m_saiObjectCollection;

            AttrKeyMap m_attrKeys;

        private: // unittests

            std::set<std::string> m_meta_unittests_set_readonly_set;

            bool m_unittestsEnabled;

        private: // warm boot

            bool m_warmBoot;
    };
}


