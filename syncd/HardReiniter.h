#pragma once

#include "SaiInterface.h"
#include "SaiSwitch.h"

#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>

namespace syncd
{
    class HardReiniter
    {
        public:

            typedef std::unordered_map<std::string, std::string> StringHash;
            typedef std::unordered_map<sai_object_id_t, sai_object_id_t> ObjectIdMap;

        public:

            HardReiniter(
                   _In_ std::shared_ptr<sairedis::SaiInterface> sai);

            virtual ~HardReiniter();

        public:

            std::map<sai_object_id_t, std::shared_ptr<syncd::SaiSwitch>> hardReinit();

        private:

            void readAsicState();

            void redisSetVidAndRidMap(
                    _In_ const std::unordered_map<sai_object_id_t, sai_object_id_t> &map);

            std::vector<std::string> redisGetAsicStateKeys();

            std::unordered_map<sai_object_id_t, sai_object_id_t> redisGetObjectMap(
                    _In_ const std::string &key);

            std::unordered_map<sai_object_id_t, sai_object_id_t> redisGetVidToRidMap();
            std::unordered_map<sai_object_id_t, sai_object_id_t> redisGetRidToVidMap();

        private:

            ObjectIdMap m_vidToRidMap;
            ObjectIdMap m_ridToVidMap;

            std::map<sai_object_id_t, ObjectIdMap> m_switchVidToRid;
            std::map<sai_object_id_t, ObjectIdMap> m_switchRidToVid;

            std::map<sai_object_id_t, std::vector<std::string>> m_switchMap;

            std::shared_ptr<sairedis::SaiInterface> m_vendorSai;
    };
}
