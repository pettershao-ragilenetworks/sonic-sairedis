#include "SwitchNotifications.h"

#include "swss/logger.h"

using namespace syncd;

SwitchNotifications::SlotBase::SlotBase(
        _In_ sai_switch_notifications_t sn):
    m_handler(nullptr),
    m_sn(sn)
{
    SWSS_LOG_ENTER();

    // empty
}

SwitchNotifications::SlotBase::~SlotBase()
{
    SWSS_LOG_ENTER();

    // empty
}

void SwitchNotifications::SlotBase::setHandler(
        _In_ SwitchNotifications* handler)
{
    SWSS_LOG_ENTER();

    m_handler = handler;
}

SwitchNotifications* SwitchNotifications::SlotBase::getHandler() const
{
    SWSS_LOG_ENTER();

    return m_handler;
}


void SwitchNotifications::SlotBase::onFdbEvent(
        _In_ int context,
        _In_ uint32_t count,
        _In_ const sai_fdb_event_notification_data_t *data)
{
    SWSS_LOG_ENTER();

    return m_slots[context]->m_handler->onFdbEvent(count,data);
}

void SwitchNotifications::SlotBase::onPortStateChange(
        _In_ int context,
        _In_ uint32_t count,
        _In_ const sai_port_oper_status_notification_t *data)
{
    SWSS_LOG_ENTER();

    return m_slots[context]->m_handler->onPortStateChange(count, data);
}

void SwitchNotifications::SlotBase::onQueuePfcDeadlock(
        _In_ int context,
        _In_ uint32_t count,
        _In_ const sai_queue_deadlock_notification_data_t *data)
{
    SWSS_LOG_ENTER();

    return m_slots[context]->m_handler->onQueuePfcDeadlock(count, data);
}

void SwitchNotifications::SlotBase::onSwitchShutdownRequest(
        _In_ int context,
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    return m_slots[context]->m_handler->onSwitchShutdownRequest(switch_id);
}

void SwitchNotifications::SlotBase::onSwitchStateChange(
        _In_ int context,
        _In_ sai_object_id_t switch_id,
        _In_ sai_switch_oper_status_t switch_oper_status)
{
    SWSS_LOG_ENTER();

    return m_slots[context]->m_handler->onSwitchStateChange(switch_id, switch_oper_status);
}

const sai_switch_notifications_t& SwitchNotifications::SlotBase::getSwitchNotifications() const
{
    SWSS_LOG_ENTER();

    return m_sn;
}

SwitchNotifications::SlotBase* SwitchNotifications::m_slots[] =
{
    new SwitchNotifications::Slot<0x00>(),
    new SwitchNotifications::Slot<0x01>(),
    new SwitchNotifications::Slot<0x02>(),
    new SwitchNotifications::Slot<0x03>(),
    new SwitchNotifications::Slot<0x04>(),
    new SwitchNotifications::Slot<0x05>(),
    new SwitchNotifications::Slot<0x06>(),
    new SwitchNotifications::Slot<0x07>(),
    new SwitchNotifications::Slot<0x08>(),
    new SwitchNotifications::Slot<0x09>(),
    new SwitchNotifications::Slot<0x0A>(),
    new SwitchNotifications::Slot<0x0B>(),
    new SwitchNotifications::Slot<0x0C>(),
    new SwitchNotifications::Slot<0x0D>(),
    new SwitchNotifications::Slot<0x0E>(),
    new SwitchNotifications::Slot<0x0F>(),
    new SwitchNotifications::Slot<0x10>(),
};

SwitchNotifications::SwitchNotifications()
{
    SWSS_LOG_ENTER();

    int max = sizeof(SwitchNotifications::m_slots)/sizeof(SwitchNotifications::m_slots[0]);

    for (int i = 0; i < max; i ++) 
    {
        if (m_slots[i]->getHandler() == nullptr)
        {
            m_slot = m_slots[i];

            m_slot->setHandler(this);

            return;
        }
    }

    SWSS_LOG_THROW("no more available slots, max slots: %d", max);
}

SwitchNotifications::~SwitchNotifications()
{
    SWSS_LOG_ENTER();

    m_slot->setHandler(nullptr);
}

const sai_switch_notifications_t& SwitchNotifications::getSwitchNotifications() const
{
    SWSS_LOG_ENTER();

    return m_slot->getSwitchNotifications();
}
