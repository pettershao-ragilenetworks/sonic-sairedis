#pragma once

extern "C" {
#include "saimetadata.h"
}

#include "swss/table.h"

#include <string>
#include <unordered_map>
#include <set>
#include <memory>
#include <vector>

namespace syncd
{
    class RedisClient
    {
        public:

            RedisClient();

            virtual ~RedisClient();

        public:

            void clearLaneMap(
                    _In_ sai_object_id_t switchVid) const;

            std::unordered_map<sai_uint32_t, sai_object_id_t> getLaneMap(
                    _In_ sai_object_id_t switchVid) const;

            void saveLaneMap(
                    _In_ sai_object_id_t switchVid,
                    _In_ const std::unordered_map<sai_uint32_t, sai_object_id_t>& map) const;

            std::unordered_map<sai_object_id_t, sai_object_id_t> getVidToRidMap(
                    _In_ sai_object_id_t switchVid) const;

            std::unordered_map<sai_object_id_t, sai_object_id_t> getRidToVidMap(
                    _In_ sai_object_id_t switchVid) const;

            std::unordered_map<sai_object_id_t, sai_object_id_t> getVidToRidMap() const;

            std::unordered_map<sai_object_id_t, sai_object_id_t> getRidToVidMap() const;

            void setDummyAsicStateObject(
                    _In_ sai_object_id_t objectVid);

            void saveColdBootDiscoveredVids(
                    _In_ sai_object_id_t switchVid,
                    _In_ const std::set<sai_object_id_t>& coldVids);

            std::shared_ptr<std::string> getSwitchHiddenAttribute(
                    _In_ sai_object_id_t switchVid,
                    _In_ const std::string& attrIdName);

            void saveSwitchHiddenAttribute(
                    _In_ sai_object_id_t switchVid,
                    _In_ const std::string& attrIdName,
                    _In_ sai_object_id_t objectRid);

            std::set<sai_object_id_t> getColdVids(
                    _In_ sai_object_id_t switchVid);

            void setPortLanes(
                    _In_ sai_object_id_t switchVid,
                    _In_ sai_object_id_t portRid,
                    _In_ const std::vector<uint32_t>& lanes);

            size_t getAsicObjectsSize(
                    _In_ sai_object_id_t switchVid) const;

            int removePortFromLanesMap(
                    _In_ sai_object_id_t switchVid,
                    _In_ sai_object_id_t portRid) const;

            std::shared_ptr<std::string> getVidForRid(
                    _In_ sai_object_id_t objectRid) const;

            void removeAsicObject(
                    _In_ sai_object_id_t objectVid) const;

            void removeAsicObject(
                    _In_ const sai_object_meta_key_t& metaKey);

            void createAsicObject(
                    _In_ const sai_object_meta_key_t& metaKey,
                    _In_ const std::vector<swss::FieldValueTuple>& attrs);

            void setVidAndRidMap(
                    _In_ const std::unordered_map<sai_object_id_t, sai_object_id_t>& map);

            std::vector<std::string> getAsicStateKeys() const;

            void removeColdVid(
                    _In_ sai_object_id_t vid);

            std::unordered_map<std::string, std::string> getAttributesFromAsicKey(
                    _In_ const std::string& key) const;

        private:

            std::string getRedisLanesKey(
                    _In_ sai_object_id_t switchVid) const;

            std::string getRedisColdVidsKey(
                    _In_ sai_object_id_t switchVid) const;

            std::string getRedisHiddenKey(
                    _In_ sai_object_id_t switchVid) const;

            std::unordered_map<sai_object_id_t, sai_object_id_t> getObjectMap(
                    _In_ const std::string& key) const;

    };
}
