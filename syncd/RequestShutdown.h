#pragma once

#include "RequestShutdownCommandLineOptions.h"

#include "swss/sal.h"

#include <memory>

#define SYNCD_NOTIFICATION_CHANNEL_RESTARTQUERY     "RESTARTQUERY"

namespace syncd
{
    class RequestShutdown
    {
        public:

            RequestShutdown(
                    _In_ std::shared_ptr<RequestShutdownCommandLineOptions> options);

            virtual ~RequestShutdown();

        public:

            void send();

        private:

            std::shared_ptr<RequestShutdownCommandLineOptions> m_options;
    };
}
