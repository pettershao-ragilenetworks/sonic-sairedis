#pragma once

#include "lib/inc/OidIndexGenerator.h"

namespace saimeta
{
    class NumberOidIndexGenerator:
        public sairedis::OidIndexGenerator
    {

        public:

            NumberOidIndexGenerator();

            virtual ~NumberOidIndexGenerator() = default;

        public:

            virtual uint64_t increment() override;

            virtual void reset() override;

        private:

            uint64_t m_index;
    };
}
