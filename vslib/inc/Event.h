#pragma once

#include "EventPayload.h"

#include "swss/sal.h"

#include <memory>

namespace saivs
{
    typedef enum _EventType
    {
        EVENT_TYPE_NET_LINK_MESSAGE,


    } EventType;

    class Event
    {
        public:

            Event(
                    _In_ EventType eventType,
                    _In_ std::shared_ptr<EventPayload> payload);

            virtual ~Event() = default;

        public:

            EventType getEventType() const;

            std::shared_ptr<EventPayload> getPayload() const;

        private:

            EventType m_eventType;

            std::shared_ptr<EventPayload> m_payload;
    };
}
