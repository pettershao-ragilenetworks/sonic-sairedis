#pragma once

#include "CommandLineOptions.h"
#include "FlexCounterManager.h"

#include <memory>

namespace syncd
{
    class Syncd
    {
        private:

            Syncd(const Syncd&) = delete;
            Syncd& operator=(const Syncd&) = delete;

            public:

            Syncd(
                    _In_ std::shared_ptr<CommandLineOptions> cmd,
                    _In_ bool isWarmStart);

            virtual ~Syncd();

        public:

            bool getAsicInitViewMode() const;

            void setAsicInitViewMode(
                    _In_ bool enable);


        private:

            std::shared_ptr<CommandLineOptions> m_commandLineOptions;

            bool m_isWarmStart;

        public: // TODO to private

            bool m_asicInitViewMode;

            std::shared_ptr<FlexCounterManager> m_manager;

    };
}
