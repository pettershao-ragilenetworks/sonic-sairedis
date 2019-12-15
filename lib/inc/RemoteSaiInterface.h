#pragma once

#include "SaiInterface.h"

namespace sairedis
{
    class RemoteSaiInterface:
        public SaiInterface
    {
        public:

            RemoteSaiInterface() = default;

            virtual ~RemoteSaiInterface() = default;

        public:

            // TODO notify syncd methods
    };
}
