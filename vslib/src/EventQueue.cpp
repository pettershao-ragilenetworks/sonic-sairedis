#include "EventQueue.h"

#include "swss/logger.h"

using namespace saivs;

#define MUTEX std::lock_guard<std::mutex> _lock(m_mutex);

void EventQueue::enqueue(
        _In_ std::shared_ptr<Event> event)
{
    SWSS_LOG_ENTER();

    MUTEX;

    m_queue.push_back(event);

    // TODO signal !

}

std::shared_ptr<Event> EventQueue::dequeue()
{
    SWSS_LOG_ENTER();

    MUTEX;

    if (m_queue.size())
    {
        auto item = m_queue.front();

        m_queue.pop_front();

        return item;
    }

    return nullptr;
}

size_t EventQueue::size()
{
    SWSS_LOG_ENTER();

    MUTEX;

    return m_queue.size();
}
