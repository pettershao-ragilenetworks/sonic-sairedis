#include "HostInterfaceInfo.h"
#include "SwitchStateBase.h"
#include "SelectableFd.h"

#include "swss/logger.h"
#include "swss/select.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <unistd.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <linux/if_packet.h>
#include <net/if_arp.h>
#include <linux/if_ether.h>

#include "sai_vs.h" // TODO to be removed
#include "sai_vs_internal.h" // TODO to be removed

using namespace saivs;

HostInterfaceInfo::HostInterfaceInfo(
        _In_ int ifindex,
        _In_ int socket,
        _In_ int tapfd,
        _In_ const std::string& tapname,
        _In_ sai_object_id_t portId):
    m_ifindex(ifindex),
    m_packet_socket(socket),
    m_name(tapname),
    m_portId(portId),
    m_tapfd(tapfd)
{
    SWSS_LOG_ENTER();

    m_run_thread = true;

    m_e2t = std::make_shared<std::thread>(&HostInterfaceInfo::veth2tap_fun, this);
    m_t2e = std::make_shared<std::thread>(&HostInterfaceInfo::tap2veth_fun, this);
}

HostInterfaceInfo::~HostInterfaceInfo()
{
    SWSS_LOG_ENTER();

    m_run_thread = false;

    m_e2tEvent.notify();
    m_t2eEvent.notify();

    if (m_t2e)
    {
        m_t2e->join();
    }

    if (m_e2t)
    {
        m_e2t->join();
    }

    // remove tap device

    int err = close(m_tapfd);

    if (err)
    {
        SWSS_LOG_ERROR("failed to remove tap device: %s, err: %d", m_name.c_str(), err);
    }

    SWSS_LOG_NOTICE("joined threads for hostif: %s", m_name.c_str());
}

void updateLocalDB(
        _In_ const sai_fdb_event_notification_data_t &data,
        _In_ sai_fdb_event_t fdb_event)
{
    SWSS_LOG_ENTER();

    sai_status_t status;

    switch (fdb_event)
    {
        case SAI_FDB_EVENT_LEARNED:

            {
                status = vs_generic_create_fdb_entry(&data.fdb_entry, data.attr_count, data.attr);

                if (status != SAI_STATUS_SUCCESS)
                {
                    SWSS_LOG_ERROR("failed to create fdb entry: %s",
                            sai_serialize_fdb_entry(data.fdb_entry).c_str());
                }
            }

            break;

        case SAI_FDB_EVENT_AGED:

            {
                status = vs_generic_remove_fdb_entry(&data.fdb_entry);

                if (status != SAI_STATUS_SUCCESS)
                {
                    SWSS_LOG_ERROR("failed to remove fdb entry %s",
                            sai_serialize_fdb_entry(data.fdb_entry).c_str());
                }
            }

            break;

        default:
            SWSS_LOG_ERROR("unsupported fdb event: %d", fdb_event);
            break;
    }
}

void processFdbInfo(
        _In_ const FdbInfo &fi,
        _In_ sai_fdb_event_t fdb_event)
{
    SWSS_LOG_ENTER();

    sai_attribute_t attrs[2];

    attrs[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    attrs[0].value.s32 = SAI_FDB_ENTRY_TYPE_DYNAMIC;

    attrs[1].id = SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID;
    attrs[1].value.oid = fi.getBridgePortId();

    sai_fdb_event_notification_data_t data;

    data.event_type = fdb_event;

    data.fdb_entry = fi.getFdbEntry();

    data.attr_count = 2;
    data.attr = attrs;

    // update metadata DB
    meta_sai_on_fdb_event(1, &data);

    // update local DB
    updateLocalDB(data, fdb_event);

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY;

    sai_status_t status = vs_generic_get(SAI_OBJECT_TYPE_SWITCH, data.fdb_entry.switch_id, 1, &attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to get fdb event notify from switch %s",
                sai_serialize_object_id(data.fdb_entry.switch_id).c_str());
        return;
    }

    std::string s = sai_serialize_fdb_event_ntf(1, &data);

    SWSS_LOG_DEBUG("calling user fdb event callback: %s", s.c_str());

    sai_fdb_event_notification_fn ntf = (sai_fdb_event_notification_fn)attr.value.ptr;

    if (ntf != NULL)
    {
        ntf(1, &data);
    }
}


bool getLagFromPort(
        _In_ sai_object_id_t port_id,
        _Inout_ sai_object_id_t& lag_id);


void findBridgeVlanForPortVlan(
        _In_ sai_object_id_t port_id,
        _In_ sai_vlan_id_t vlan_id,
        _Inout_ sai_object_id_t &bv_id,
        _Inout_ sai_object_id_t &bridge_port_id)
{
    SWSS_LOG_ENTER();

    bv_id = SAI_NULL_OBJECT_ID;
    bridge_port_id = SAI_NULL_OBJECT_ID;

    sai_object_id_t bridge_id;

    /*
     * The bridge port lookup process is two steps:
     *
     * - use (vlan_id, physical port_id) to match any .1D bridge port created.
     *   If there is match, then quit, found=true
     *
     * - use (physical port_id) to match any .1Q bridge created. if there is a
     *   match, the quite, found=true.
     *
     * If found==true, generate fdb learn event on the .1D or .1Q bridge port.
     * If not found, then do not generate fdb event. It means the packet is not
     * received on the bridge port.
     *
     * XXX: this is not whats happening here, we are just looking for any
     * bridge id (as in our case this is shortcut, we will remove all bridge ports
     * when we will use router interface based port/lag and no bridge
     * will be found.
     */

    sai_object_id_t switch_id = g_realObjectIdManager->saiSwitchIdQuery(port_id);

    auto &objectHash = g_switch_state_map.at(switch_id)->m_objectHash.at(SAI_OBJECT_TYPE_BRIDGE_PORT);

    // iterate via all bridge ports to find match on port id

    sai_object_id_t lag_id = SAI_NULL_OBJECT_ID;

    if (getLagFromPort(port_id,lag_id))
    {
        SWSS_LOG_INFO("got lag %s for port %s",
                sai_serialize_object_id(lag_id).c_str(),
                sai_serialize_object_id(port_id).c_str());
    }

    bool bv_id_set = false;

    for (auto it = objectHash.begin(); it != objectHash.end(); ++it)
    {
        sai_object_id_t bpid;

        sai_deserialize_object_id(it->first, bpid);

        sai_attribute_t attrs[2];

        attrs[0].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
        attrs[1].id = SAI_BRIDGE_PORT_ATTR_TYPE;

        sai_status_t status = vs_generic_get(SAI_OBJECT_TYPE_BRIDGE_PORT, bpid, (uint32_t)(sizeof(attrs)/sizeof(attrs[0])), attrs);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_WARN("failed to get attr PORT_ID and TYPE for bridge port %s",
                    sai_serialize_object_id(bpid).c_str());
            continue;
        }

        if (lag_id != SAI_NULL_OBJECT_ID)
        {
            // if port is member of lag, we should check if port_id is that LAG

            if (port_id == attrs[0].value.oid)
            {
                // there should be no case that the same port is lag member and has bridge port object on it

                SWSS_LOG_ERROR("port %s is member of lag %s, and also has bridge port created: %s",
                        sai_serialize_object_id(port_id).c_str(),
                        sai_serialize_object_id(lag_id).c_str(),
                        sai_serialize_object_id(attrs[0].value.oid).c_str());
                continue;
            }

            if (lag_id != attrs[0].value.oid)
            {
                // this is not expected port
                continue;
            }
        }
        else if (port_id != attrs[0].value.oid)
        {
            // this is not expected port
            continue;
        }

        bridge_port_id = bpid;

        // get the 1D bridge id if the bridge port type is subport
        auto bp_type = attrs[1].value.s32;

        SWSS_LOG_DEBUG("found bridge port %s of type %d",
                sai_serialize_object_id(bridge_port_id).c_str(),
                bp_type);

        if (bp_type == SAI_BRIDGE_PORT_TYPE_SUB_PORT)
        {
            sai_attribute_t attr;
            attr.id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;

            status = vs_generic_get(SAI_OBJECT_TYPE_BRIDGE_PORT, bridge_port_id, 1, &attr);

            if (status != SAI_STATUS_SUCCESS)
            {
                break;
            }

            bridge_id = attr.value.oid;

            SWSS_LOG_DEBUG("found bridge %s for port %s",
                    sai_serialize_object_id(bridge_id).c_str(),
                    sai_serialize_object_id(port_id).c_str());

            attr.id = SAI_BRIDGE_ATTR_TYPE;

            status = vs_generic_get(SAI_OBJECT_TYPE_BRIDGE, bridge_id, 1, &attr);

            if (status != SAI_STATUS_SUCCESS)
            {
                break;
            }

            SWSS_LOG_DEBUG("bridge %s type is %d",
                    sai_serialize_object_id(bridge_id).c_str(),
                    attr.value.s32);
            bv_id = bridge_id;
            bv_id_set = true;
        }
        else
        {
            auto &objectHash2 = g_switch_state_map.at(switch_id)->m_objectHash.at(SAI_OBJECT_TYPE_VLAN);

            // iterate via all vlans to find match on vlan id

            for (auto it2 = objectHash2.begin(); it2 != objectHash2.end(); ++it2)
            {
                sai_object_id_t vlan_oid;

                sai_deserialize_object_id(it2->first, vlan_oid);

                sai_attribute_t attr;
                attr.id = SAI_VLAN_ATTR_VLAN_ID;

                status = vs_generic_get(SAI_OBJECT_TYPE_VLAN, vlan_oid, 1, &attr);

                if (status != SAI_STATUS_SUCCESS)
                {
                    continue;
                }

                if (vlan_id == attr.value.u16)
                {
                    bv_id = vlan_oid;
                    bv_id_set = true;
                    break;
                }
            }
        }

        break;
    }

    if (!bv_id_set)
    {
        // if port is lag member, then we didn't found bridge_port for that lag (expected for rif lag)
        SWSS_LOG_WARN("failed to find bv_id for vlan %d and port_id %s",
                vlan_id,
                sai_serialize_object_id(port_id).c_str());
    }
}

bool getLagFromPort(
        _In_ sai_object_id_t port_id,
        _Inout_ sai_object_id_t& lag_id)
{
    SWSS_LOG_ENTER();

    lag_id = SAI_NULL_OBJECT_ID;

    sai_object_id_t switch_id = g_realObjectIdManager->saiSwitchIdQuery(port_id);

    auto &objectHash = g_switch_state_map.at(switch_id)->m_objectHash.at(SAI_OBJECT_TYPE_LAG_MEMBER);

    // iterate via all lag members to find match on port id

    for (auto it = objectHash.begin(); it != objectHash.end(); ++it)
    {
        sai_object_id_t lag_member_id;

        sai_deserialize_object_id(it->first, lag_member_id);

        sai_attribute_t attr;

        attr.id = SAI_LAG_MEMBER_ATTR_PORT_ID;

        sai_status_t status = vs_generic_get(SAI_OBJECT_TYPE_LAG_MEMBER, lag_member_id, 1, &attr);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("failed to get port id from leg member %s",
                    sai_serialize_object_id(lag_member_id).c_str());
            continue;
        }

        if (port_id != attr.value.oid)
        {
            // this is not the port we are looking for
            continue;
        }

        attr.id = SAI_LAG_MEMBER_ATTR_LAG_ID;

        status = vs_generic_get(SAI_OBJECT_TYPE_LAG_MEMBER, lag_member_id, 1, &attr);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("failed to get lag id from lag member %s",
                    sai_serialize_object_id(lag_member_id).c_str());
            continue;
        }

        lag_id = attr.value.oid;

        return true;
    }

    // this port does not belong to any lag

    return false;
}




bool isLagOrPortRifBased(
        _In_ sai_object_id_t lag_or_port_id)
{
    SWSS_LOG_ENTER();

    sai_object_id_t switch_id = g_realObjectIdManager->saiSwitchIdQuery(lag_or_port_id);

    auto &objectHash = g_switch_state_map.at(switch_id)->m_objectHash.at(SAI_OBJECT_TYPE_ROUTER_INTERFACE);

    // iterate via all lag members to find match on port id

    for (auto it = objectHash.begin(); it != objectHash.end(); ++it)
    {
        sai_object_id_t rif_id;

        sai_deserialize_object_id(it->first, rif_id);

        sai_attribute_t attr;

        attr.id = SAI_ROUTER_INTERFACE_ATTR_TYPE;

        sai_status_t status = vs_generic_get(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rif_id, 1, &attr);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("failed to get rif type from rif %s",
                    sai_serialize_object_id(rif_id).c_str());
            continue;
        }

        switch (attr.value.s32)
        {
            case SAI_ROUTER_INTERFACE_TYPE_PORT:
            case SAI_ROUTER_INTERFACE_TYPE_SUB_PORT:
                break;

            default:
                continue;
        }

        attr.id = SAI_ROUTER_INTERFACE_ATTR_PORT_ID;

        status = vs_generic_get(SAI_OBJECT_TYPE_ROUTER_INTERFACE, rif_id, 1, &attr);

        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("failed to get rif port id from rif %s",
                    sai_serialize_object_id(rif_id).c_str());
            continue;
        }

        if (attr.value.oid == lag_or_port_id)
        {
            return true;
        }
    }

    return false;
}

void HostInterfaceInfo::process_packet_for_fdb_event(
        _In_ const uint8_t *buffer,
        _In_ size_t size) const
{
    SWSS_LOG_ENTER();

    MUTEX();
    //VS_CHECK_API_INITIALIZED();

    if (!Globals::apiInitialized)
    {
        SWSS_LOG_ERROR("%s: api not initialized", __PRETTY_FUNCTION__);
        return;
    }

    // TODO this function could be still called when switch is removed
    // during syncd shutdown

    uint32_t frametime = (uint32_t)time(NULL);

    /*
     * We add +2 in case if frame contains 1Q VLAN tag.
     */

    if (size < (sizeof(ethhdr) + 2))
    {
        SWSS_LOG_WARN("ethernet frame is too small: %zu", size);
        return;
    }

    const ethhdr *eh = (const ethhdr*)buffer;

    uint16_t proto = htons(eh->h_proto);

    uint16_t vlan_id = DEFAULT_VLAN_NUMBER;

    bool tagged = (proto == ETH_P_8021Q);

    if (tagged)
    {
        // this is tagged frame, get vlan id from frame

        uint16_t tci = htons(((const uint16_t*)&eh->h_proto)[1]); // tag is after h_proto field

        vlan_id = tci & 0xfff;

        if (vlan_id == 0xfff)
        {
            SWSS_LOG_WARN("invalid vlan id %u in ethernet frame on %s", vlan_id, m_name.c_str());
            return;
        }

        if (vlan_id == 0)
        {
            // priority packet, frame should be treated as non tagged
            tagged = false;
        }
    }

    if (tagged == false)
    {
        // untagged ethernet frame

        sai_attribute_t attr;

#ifdef SAI_LAG_ATTR_PORT_VLAN_ID

        sai_object_id_t lag_id;

        if (getLagFromPort(portid, lag_id))
        {
            // if port belongs to lag we need to get SAI_LAG_ATTR_PORT_VLAN_ID

            attr.id = SAI_LAG_ATTR_PORT_VLAN_ID

            sai_status_t status = vs_generic_get(SAI_OBJECT_TYPE_LAG, lag_id, 1, &attr);

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_WARN("failed to get lag vlan id from lag %s",
                        sai_serialize_object_id(lag_id).c_str());
                return;
            }

            vlan_id = attr.value.u16;

            if (isLagOrPortRifBased(lag_id))
            {
                // this lag is router interface based, skip mac learning
                return;
            }
        }
        else
#endif
        {
            attr.id = SAI_PORT_ATTR_PORT_VLAN_ID;

            sai_status_t status = vs_generic_get(SAI_OBJECT_TYPE_PORT, m_portId, 1, &attr);

            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_WARN("failed to get port vlan id from port %s",
                        sai_serialize_object_id(m_portId).c_str());
                return;
            }

            // untagged port vlan (default is 1, but may change setting port attr)
            vlan_id = attr.value.u16;
        }
    }

    sai_object_id_t lag_id;
    if (getLagFromPort(m_portId, lag_id) && isLagOrPortRifBased(lag_id))
    {
        SWSS_LOG_DEBUG("lag %s is rif based, skip mac learning for port %s",
                sai_serialize_object_id(lag_id).c_str(),
                sai_serialize_object_id(m_portId).c_str());
        return;
    }

    if (isLagOrPortRifBased(m_portId))
    {
        SWSS_LOG_DEBUG("port %s is rif based, skip mac learning",
                sai_serialize_object_id(m_portId).c_str());
        return;
    }

    // we have vlan and mac address which is KEY, so just see if that is already defined

    FdbInfo fi;

    fi.setPortId((lag_id != SAI_NULL_OBJECT_ID) ? lag_id : m_portId);

    fi.setVlanId(vlan_id);

    memcpy(fi.m_fdbEntry.mac_address, eh->h_source, sizeof(sai_mac_t));

    if (g_switch_state_map.size()== 0)
    {
        // fdb arrived on destroyed switch, we need to move processing to switch
        // and remove will close this thread
        SWSS_LOG_WARN("g_switch_state_map.size is ZERO, but %s called, logic error, FIXME", __PRETTY_FUNCTION__);
        return;
    }

    // TODO cast right switch or different data pass
    auto ss = std::dynamic_pointer_cast<SwitchStateBase>(g_switch_state_map.begin()->second);

    std::set<FdbInfo>::iterator it = ss->m_fdb_info_set.find(fi);

    if (it != ss->m_fdb_info_set.end())
    {
        // this key was found, update timestamp
        // and since iterator is const we need to reinsert

        fi = *it;

        fi.setTimestamp(frametime);

        ss->m_fdb_info_set.insert(fi);

        return;
    }

    // key was not found, get additional information

    fi.setTimestamp(frametime);

    fi.m_fdbEntry.switch_id = g_realObjectIdManager->saiSwitchIdQuery(m_portId);

    findBridgeVlanForPortVlan(m_portId, vlan_id, fi.m_fdbEntry.bv_id, fi.m_bridgePortId);

    if (fi.getFdbEntry().bv_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_WARN("skipping mac learn for %s, since BV_ID was not found for mac",
                sai_serialize_fdb_entry(fi.getFdbEntry()).c_str());

        // bridge was not found, skip mac learning
        return;
    }

    SWSS_LOG_INFO("inserting to fdb_info set: %s, vid: %d",
            sai_serialize_fdb_entry(fi.getFdbEntry()).c_str(),
            fi.getVlanId());

    ss->m_fdb_info_set.insert(fi);

    processFdbInfo(fi, SAI_FDB_EVENT_LEARNED);
}

#define ETH_FRAME_BUFFER_SIZE (0x4000)
#define CONTROL_MESSAGE_BUFFER_SIZE (0x1000)
#define IEEE_8021Q_ETHER_TYPE (0x8100)
#define MAC_ADDRESS_SIZE (6)
#define VLAN_TAG_SIZE (4)

void HostInterfaceInfo::veth2tap_fun()
{
    SWSS_LOG_ENTER();

    unsigned char buffer[ETH_FRAME_BUFFER_SIZE];

    swss::Select s;
    SelectableFd fd(m_packet_socket);

    s.addSelectable(&m_e2tEvent);
    s.addSelectable(&fd);

    while (m_run_thread)
    {
        struct msghdr  msg;
        memset(&msg, 0, sizeof(struct msghdr));

        struct sockaddr_storage src_addr;

        struct iovec iov[1];

        iov[0].iov_base = buffer;       // buffer for message
        iov[0].iov_len = sizeof(buffer);

        char control[CONTROL_MESSAGE_BUFFER_SIZE];   // buffer for control messages

        msg.msg_name = &src_addr;
        msg.msg_namelen = sizeof(src_addr);
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        swss::Selectable *sel = NULL;

        int result = s.select(&sel);

        if (result != swss::Select::OBJECT)
        {
            SWSS_LOG_ERROR("selectable failed: %d, ending thread for %s", result, m_name.c_str());
            return;
        }

        if (sel == &m_e2tEvent) // thread end event
            break;

        ssize_t size = recvmsg(m_packet_socket, &msg, 0);

        if (size < 0)
        {
            SWSS_LOG_ERROR("failed to read from socket fd %d, errno(%d): %s",
                    m_packet_socket, errno, strerror(errno));

            continue;
        }

        if (size < (ssize_t)sizeof(ethhdr))
        {
            SWSS_LOG_ERROR("invalid ethernet frame length: %zu", msg.msg_controllen);
            continue;
        }

        struct cmsghdr *cmsg;

        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg))
        {
            if (cmsg->cmsg_level != SOL_PACKET || cmsg->cmsg_type != PACKET_AUXDATA)
                continue;

            struct tpacket_auxdata* aux = (struct tpacket_auxdata*)CMSG_DATA(cmsg);

            if ((aux->tp_status & TP_STATUS_VLAN_VALID) &&
                    (aux->tp_status & TP_STATUS_VLAN_TPID_VALID))
            {
                SWSS_LOG_DEBUG("got vlan tci: 0x%x, vlanid: %d", aux->tp_vlan_tci, aux->tp_vlan_tci & 0xFFF);

                // inject vlan tag into frame

                // for overlapping buffers
                memmove(buffer + 2 * MAC_ADDRESS_SIZE + VLAN_TAG_SIZE,
                        buffer + 2 * MAC_ADDRESS_SIZE,
                        size - (2 * MAC_ADDRESS_SIZE));

                uint16_t tci = htons(aux->tp_vlan_tci);
                uint16_t tpid = htons(IEEE_8021Q_ETHER_TYPE);

                uint8_t* pvlan =  (uint8_t *)(buffer + 2 * MAC_ADDRESS_SIZE);
                memcpy(pvlan, &tpid, sizeof(uint16_t));
                memcpy(pvlan + sizeof(uint16_t), &tci, sizeof(uint16_t));

                size += VLAN_TAG_SIZE;

                break;
            }
        }

        process_packet_for_fdb_event(buffer, size);

        if (write(m_tapfd, buffer, size) < 0)
        {
            /*
             * We filter out EIO because of this patch:
             * https://github.com/torvalds/linux/commit/1bd4978a88ac2589f3105f599b1d404a312fb7f6
             */

            if (errno != ENETDOWN && errno != EIO)
            {
                SWSS_LOG_ERROR("failed to write to tap device fd %d, errno(%d): %s",
                        m_tapfd, errno, strerror(errno));
            }

            if (errno == EBADF)
            {
                // bad file descriptor, just end thread
                SWSS_LOG_NOTICE("ending thread for tap fd %d", m_tapfd);
                return;
            }

            continue;
        }
    }

    SWSS_LOG_NOTICE("ending thread proc for %s", m_name.c_str());
}

void HostInterfaceInfo::tap2veth_fun()
{
    SWSS_LOG_ENTER();

    unsigned char buffer[ETH_FRAME_BUFFER_SIZE];

    swss::Select s;
    SelectableFd fd(m_tapfd);

    s.addSelectable(&m_t2eEvent);
    s.addSelectable(&fd);

    while (m_run_thread)
    {
        swss::Selectable *sel = NULL;

        int result = s.select(&sel);

        if (result != swss::Select::OBJECT)
        {
            SWSS_LOG_ERROR("selectable failed: %d, ending thread for %s", result, m_name.c_str());
            return;
        }

        if (sel == &m_t2eEvent) // thread end event
            break;

        ssize_t size = read(m_tapfd, buffer, sizeof(buffer));

        if (size < 0)
        {
            SWSS_LOG_ERROR("failed to read from tapfd fd %d, errno(%d): %s",
                    m_tapfd, errno, strerror(errno));

            if (errno == EBADF)
            {
                // bad file descriptor, just close the thread
                SWSS_LOG_NOTICE("ending thread for tap fd %d", m_tapfd);
                return;
            }

            continue;
        }

        if (write(m_packet_socket, buffer, (int)size) < 0)
        {
            SWSS_LOG_ERROR("failed to write to socket fd %d, errno(%d): %s",
                    m_packet_socket, errno, strerror(errno));

            continue;
        }
    }

    SWSS_LOG_NOTICE("ending thread proc for %s", m_name.c_str());
}
