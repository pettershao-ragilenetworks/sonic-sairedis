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


sai_status_t SwitchBCM56850::create_scheduler_group_tree(
        _In_ const std::vector<sai_object_id_t>& sgs,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    sai_attribute_t attrq;

    std::vector<sai_object_id_t> queues;

    // in this implementation we have 20 queues per port
    // (10 in and 10 out), which will be assigned to schedulers
    uint32_t queues_count = 20;

    queues.resize(queues_count);

    attrq.id = SAI_PORT_ATTR_QOS_QUEUE_LIST;
    attrq.value.objlist.count = queues_count;
    attrq.value.objlist.list = queues.data();

    // NOTE it will do recalculate
    CHECK_STATUS(vs_generic_get(SAI_OBJECT_TYPE_PORT, port_id, 1, &attrq));

    // schedulers groups: 0 1 2 3 4 5 6 7 8 9 a b c

    // tree index
    // 0 = 1 2
    // 1 = 3 4 5 6 7 8 9 a
    // 2 = b c (bug on brcm)

    // 3..c - have both QUEUES, each one 2

    // scheduler group 0 (2 groups)
    {
        sai_object_id_t sg_0 = sgs.at(0);

        sai_attribute_t attr;

        attr.id = SAI_SCHEDULER_GROUP_ATTR_PORT_ID;
        attr.value.oid = port_id;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_0, &attr));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
        attr.value.u32 = 2;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_0, &attr));

        uint32_t list_count = 2;
        std::vector<sai_object_id_t> list;

        list.push_back(sgs.at(1));
        list.push_back(sgs.at(2));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
        attr.value.objlist.count = list_count;
        attr.value.objlist.list = list.data();

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_0, &attr));
    }

    uint32_t queue_index = 0;

    // scheduler group 1 (8 groups)
    {
        sai_object_id_t sg_1 = sgs.at(1);

        sai_attribute_t attr;

        attr.id = SAI_SCHEDULER_GROUP_ATTR_PORT_ID;
        attr.value.oid = port_id;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_1, &attr));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
        attr.value.u32 = 8;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_1, &attr));

        uint32_t list_count = 8;
        std::vector<sai_object_id_t> list;

        list.push_back(sgs.at(3));
        list.push_back(sgs.at(4));
        list.push_back(sgs.at(5));
        list.push_back(sgs.at(6));
        list.push_back(sgs.at(7));
        list.push_back(sgs.at(8));
        list.push_back(sgs.at(9));
        list.push_back(sgs.at(0xa));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
        attr.value.objlist.count = list_count;
        attr.value.objlist.list = list.data();

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_1, &attr));

        // now assign queues to level 1 scheduler groups,

        for (size_t i = 0; i < list.size(); ++i)
        {
            sai_object_id_t childs[2];

            childs[0] = queues[queue_index];    // first half are in queues
            childs[1] = queues[queue_index + queues_count/2]; // second half are out queues

            // for each scheduler set 2 queues
            attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
            attr.value.objlist.count = 2;
            attr.value.objlist.list = childs;

            queue_index++;

            CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, list.at(i), &attr));

            attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
            attr.value.u32 = 2;

            CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, list.at(i), &attr));
        }
    }

    // scheduler group 2 (2 groups)
    {
        sai_object_id_t sg_2 = sgs.at(2);

        sai_attribute_t attr;

        attr.id = SAI_SCHEDULER_GROUP_ATTR_PORT_ID;
        attr.value.oid = port_id;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_2, &attr));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
        attr.value.u32 = 2;

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_2, &attr));

        uint32_t list_count = 2;
        std::vector<sai_object_id_t> list;

        list.push_back(sgs.at(0xb));
        list.push_back(sgs.at(0xc));

        attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
        attr.value.objlist.count = list_count;
        attr.value.objlist.list = list.data();

        CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, sg_2, &attr));

        for (size_t i = 0; i < list.size(); ++i)
        {
            sai_object_id_t childs[2];

            // for each scheduler set 2 queues
            childs[0] = queues[queue_index];    // first half are in queues
            childs[1] = queues[queue_index + queues_count/2]; // second half are out queues

            attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST;
            attr.value.objlist.count = 2;
            attr.value.objlist.list = childs;

            queue_index++;

            CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, list.at(i), &attr));

            attr.id = SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT;
            attr.value.u32 = 2;

            CHECK_STATUS(set(SAI_OBJECT_TYPE_SCHEDULER_GROUP, list.at(i), &attr));
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchBCM56850::create_scheduler_groups_per_port(
        _In_ sai_object_id_t switch_id,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    uint32_t port_sgs_count = 13; // brcm default

    // NOTE: this is only static data, to keep track of this
    // we would need to create actual objects and keep them
    // in respected objects, we need to move in to that
    // solution when we will start using different "profiles"
    // currently this is good enough

    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_QOS_NUMBER_OF_SCHEDULER_GROUPS;
    attr.value.u32 = port_sgs_count;

    CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    // scheduler groups per port

    std::vector<sai_object_id_t> sgs;

    for (uint32_t i = 0; i < port_sgs_count; ++i)
    {
        sai_object_id_t sg_id;

        CHECK_STATUS(create(SAI_OBJECT_TYPE_SCHEDULER_GROUP, &sg_id, m_switch_id, 0, NULL));

        sgs.push_back(sg_id);
    }

    attr.id = SAI_PORT_ATTR_QOS_SCHEDULER_GROUP_LIST;
    attr.value.objlist.count = port_sgs_count;
    attr.value.objlist.list = sgs.data();

    CHECK_STATUS(set(SAI_OBJECT_TYPE_PORT, port_id, &attr));

    CHECK_STATUS(create_scheduler_group_tree(sgs, port_id));

    // TODO
    // SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT // sched_groups + count
    // scheduler group are organized in tree and on the bottom there are queues
    // order matters in returning api

    return SAI_STATUS_SUCCESS;
}

sai_status_t SwitchBCM56850::set_maximum_number_of_childs_per_scheduler_group()
{
    SWSS_LOG_ENTER();

    SWSS_LOG_INFO("create switch src mac address");

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_CHILDS_PER_SCHEDULER_GROUP;
    attr.value.u32 = 16;

    return set(SAI_OBJECT_TYPE_SWITCH, m_switch_id, &attr);
}

