#pragma once

#include "CommandLineOptions.h"
#include "FlexCounterManager.h"

#include "swss/consumertable.h"
#include "swss/producertable.h"

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

            bool isInitViewMode() const;

        public: // TODO private

            void processEvent(
                    _In_ swss::ConsumerTable &consumer);

        private:

            sai_status_t processSingleEvent(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processAttrEnumValuesCapabilityQuery(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processObjectTypeGetAvailabilityQuery(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processFdbFlush(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processClearStatsEvent(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

            sai_status_t processGetStatsEvent(
                    _In_ const swss::KeyOpFieldsValuesTuple &kco);

        private:

            std::shared_ptr<CommandLineOptions> m_commandLineOptions;

            bool m_isWarmStart;

        public: // TODO to private

            bool m_asicInitViewMode;

            std::shared_ptr<FlexCounterManager> m_manager;

            std::shared_ptr<swss::ProducerTable> m_getResponse;

    };
}
