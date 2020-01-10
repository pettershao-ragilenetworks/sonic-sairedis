#include "SwitchBCM56850.h"

#include "swss/logger.h"

#include "sai_vs.h" // TODO to be removed

using namespace saivs;


SwitchBCM56850::SwitchBCM56850(
        _In_ sai_object_id_t switch_id):
    SwitchStateBase(switch_id)
{
    SWSS_LOG_ENTER();

    // empty
}

sai_status_t SwitchBCM56850::create_qos_queues_per_port(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    sai_attribute_t attr;

    // 10 in and 10 out queues per port
    const uint32_t port_qos_queues_count = 20;

    std::vector<sai_object_id_t> queues;

    for (uint32_t i = 0; i < port_qos_queues_count; ++i)
    {
        sai_object_id_t queue_id;

        CHECK_STATUS(create(SAI_OBJECT_TYPE_QUEUE, &queue_id, switch_id, 0, NULL));

        queues.push_back(queue_id);

        attr.id = SAI_QUEUE_ATTR_TYPE;
        attr.value.s32 = (i < port_qos_queues_count / 2) ?  SAI_QUEUE_TYPE_UNICAST : SAI_QUEUE_TYPE_MULTICAST;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_QUEUE, queue_id, &attr));

        attr.id = SAI_QUEUE_ATTR_INDEX;
        attr.value.u8 = (uint8_t)i;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_QUEUE, queue_id, &attr));
    }

    attr.id = SAI_PORT_ATTR_QOS_NUMBER_OF_QUEUES;
    attr.value.u32 = port_qos_queues_count;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    attr.id = SAI_PORT_ATTR_QOS_QUEUE_LIST;
    attr.value.objlist.count = port_qos_queues_count;
    attr.value.objlist.list = queues.data();

    CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchBCM56850::create_qos_queues()
{
    SWSS_LOG_ENTER();

    // TODO queues size may change when we will modify queue or ports

    SWSS_LOG_INFO("create qos queues");

    for (auto &port_id : m_port_list)
    {
        CHECK_STATUS(create_qos_queues_per_port(m_switch_id, port_id));
    }

    return SAI_STATUS_SUCCESS;
}

