#pragma once

#include <string>

namespace syncd
{
    class RequestShutdownCommandLineOptions
    {
        public:

            RequestShutdownCommandLineOptions();

            virtual ~RequestShutdownCommandLineOptions();

        public:

            std::string m_op;
    };
}
