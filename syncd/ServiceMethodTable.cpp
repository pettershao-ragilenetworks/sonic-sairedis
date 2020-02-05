#include "ServiceMethodTable.h"

#include "swss/logger.h"

using namespace syncd;

ServiceMethodTable::SlotBase::SlotBase(
        _In_ sai_service_method_table_t smt):
    m_handler(nullptr),
    m_smt(smt)
{
    SWSS_LOG_ENTER();

    // empty
}

ServiceMethodTable::SlotBase::~SlotBase()
{
    SWSS_LOG_ENTER();

    // empty
}

void ServiceMethodTable::SlotBase::setHandler(
        _In_ ServiceMethodTable* handler)
{
    SWSS_LOG_ENTER();

    m_handler = handler;
}

ServiceMethodTable* ServiceMethodTable::SlotBase::getHandler() const
{
    SWSS_LOG_ENTER();

    return m_handler;
}

const char* ServiceMethodTable::SlotBase::profileGetValue(
        _In_ int context,
        _In_ sai_switch_profile_id_t profile_id,
        _In_ const char* variable)
{
    SWSS_LOG_ENTER();

    return m_slots[context]->m_handler->profileGetValue(profile_id, variable);
}

int ServiceMethodTable::SlotBase::profileGetNextValue(
        _In_ int context,
        _In_ sai_switch_profile_id_t profile_id,
        _Out_ const char** variable,
        _Out_ const char** value)
{
    return m_slots[context]->m_handler->profileGetNextValue(profile_id, variable, value);
}

const sai_service_method_table_t& ServiceMethodTable::SlotBase::getServiceMethodTable() const
{
    SWSS_LOG_ENTER();

    return m_smt;
}

ServiceMethodTable::SlotBase* ServiceMethodTable::m_slots[] =
{
    new ServiceMethodTable::Slot<0>(),
    new ServiceMethodTable::Slot<1>(),
};

ServiceMethodTable::ServiceMethodTable()
{
    SWSS_LOG_ENTER();

    int max = sizeof(ServiceMethodTable::m_slots)/sizeof(ServiceMethodTable::m_slots[0]);

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

ServiceMethodTable::~ServiceMethodTable()
{
    SWSS_LOG_ENTER();

    m_slot->setHandler(nullptr);
}

const sai_service_method_table_t& ServiceMethodTable::getServiceMethodTable() const
{
    SWSS_LOG_ENTER();

    return m_slot->getServiceMethodTable();
}
