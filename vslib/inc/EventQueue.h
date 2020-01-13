#pragma once

#include "Event.h"

#include <mutex>
#include <deque>

namespace saivs
{
    class EventQueue
    {
        public:

            EventQueue();

            virtual ~EventQueue();

        public:

            void enqueue(
                    _In_ std::shared_ptr<Event> event);

            std::shared_ptr<Event> dequeue();

            size_t size();

        private:

            std::mutex m_mutex;

            std::deque<std::shared_ptr<Event>> m_queue;
    };
}
