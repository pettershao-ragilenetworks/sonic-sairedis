#pragma once

extern "C"{
#include "saimetadata.h"
}

#include <mutex>
#include <unordered_map>

// TODO can be child class (redis translator etc)
// TODO sai_object_type_query can be functor function

namespace syncd
{
    class VirtualOidTranslator
    {
        public:

            VirtualOidTranslator() = default;

            virtual ~VirtualOidTranslator() = default;

        public:

            /*
             * This method will create VID for actual RID retrieved from device
             * when doing GET api and snooping while in init view mode.
             *
             * This function should not be used to create VID for SWITCH object
             * type.
             */
            sai_object_id_t translateRidToVid(
                    _In_ sai_object_id_t rid,
                    _In_ sai_object_id_t switchVid);

            void translateRidToVid(
                    _Inout_ sai_object_list_t& objectList,
                    _In_ sai_object_id_t switchVid);

            /*
             * This method is required to translate RID to VIDs when we are doing
             * snoop for new ID's in init view mode, on in apply view mode when we
             * are executing GET api, and new object RIDs were spotted the we will
             * create new VIDs for those objects and we will put them to redis db.
             */
            void translateRidToVid(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t switchVid,
                    _In_ uint32_t attrCount,
                    _Inout_ sai_attribute_t *attrList);

            /**
             * @brief Check if RID exists on the ASIC DB.
             *
             * @param rid Real object id to check.
             *
             * @return True if exists or SAI_NULL_OBJECT_ID, otherwise false.
             */
            bool checkRidExists(
                    _In_ sai_object_id_t rid);

            sai_object_id_t translateVidToRid(
                    _In_ sai_object_id_t vid);

            void translateVidToRid(
                    _Inout_ sai_object_list_t &element);

            void translateVidToRid(
                    _In_ sai_object_type_t objectType,
                    _In_ uint32_t attrCount,
                    _Inout_ sai_attribute_t *attrList);

            bool tryTranslateVidToRid(
                    _In_ sai_object_id_t vid,
                    _Out_ sai_object_id_t& rid);

            void translateVidToRid(
                    _Inout_ sai_object_meta_key_t &metaKey);

            void eraseRidAndVid(
                    _In_ sai_object_id_t rid,
                    _In_ sai_object_id_t vid);

            void insertRidAndVid(
                    _In_ sai_object_id_t rid,
                    _In_ sai_object_id_t vid);

            void clearLocalCache();

        private:

            std::mutex m_mutex;

            // those hashes keep mapping from all switches

            std::unordered_map<sai_object_id_t, sai_object_id_t> m_rid2vid;
            std::unordered_map<sai_object_id_t, sai_object_id_t> m_vid2rid;
    };
}
