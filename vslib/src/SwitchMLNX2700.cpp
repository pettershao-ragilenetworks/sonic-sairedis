#include "SwitchMLNX2700.h"

#include "swss/logger.h"

#include "sai_vs.h" // TODO to be removed

using namespace saivs;


SwitchMLNX2700::SwitchMLNX2700(
        _In_ sai_object_id_t switch_id):
    SwitchStateBase(switch_id)
{
    SWSS_LOG_ENTER();

    // empty
}

sai_status_t SwitchMLNX2700::create_qos_queues_per_port(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    // 8 in and 8 out queues per port
    const uint32_t port_qos_queues_count = 16;
    std::vector<sai_object_id_t> queues;

    for (uint32_t i = 0; i < port_qos_queues_count; ++i)
    {
        sai_object_id_t queue_id;

        sai_attribute_t attr[2];

        attr[0].id = SAI_QUEUE_ATTR_INDEX;
        attr[0].value.u8 = (uint8_t)i;
        attr[1].id = SAI_QUEUE_ATTR_PORT;
        attr[1].value.oid = port_id;

        CHECK_STATUS(create(SAI_OBJECT_TYPE_QUEUE, &queue_id, switch_id, 2, attr));

        queues.push_back(queue_id);
    }

    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_QOS_NUMBER_OF_QUEUES;
    attr.value.u32 = port_qos_queues_count;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    attr.id = SAI_PORT_ATTR_QOS_QUEUE_LIST;
    attr.value.objlist.count = port_qos_queues_count;
    attr.value.objlist.list = queues.data();

    CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchMLNX2700::create_qos_queues()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create qos queues");

    std::vector<sai_object_id_t> copy = m_port_list;

    copy.push_back(m_cpu_port_id);

    for (auto &port_id : copy)
    {
        create_qos_queues_per_port(m_switch_id, port_id);
    }

    return SAI_STATUS_SUCCESS;
}
