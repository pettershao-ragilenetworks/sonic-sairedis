#pragma once

extern "C" {
#include "sai.h"
}

#include <map>
#include <set>

namespace saimeta
{
    class PortRelatedSet
    {
        public:

            PortRelatedSet() = default;

            virtual ~PortRelatedSet() = default;

        public:

            void insert(
                    _In_ sai_object_id_t portId,
                    _In_ sai_object_id_t relatedObjectId);

            const std::set<sai_object_id_t> getPortRelatedObjects(
                    _In_ sai_object_id_t portId) const;

            void clear();

            void removePort(
                    _In_ sai_object_id_t portId);


        private:

            std::map<sai_object_id_t, std::set<sai_object_id_t>> m_mapset;
    };
}
