#include "saivs.h"
#include "sai_vs.h"
#include "sai_vs_internal.h"
#include "swss/selectableevent.h"
#include "swss/select.h"

#include "meta/sai_serialize.h"

#include "SelectableFd.h"
#include "SwitchStateBase.h"
#include "HostInterfaceInfo.h"

#include <string.h>
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

using namespace saivs;

#define ETH_FRAME_BUFFER_SIZE (0x4000)

#define MAX_INTERFACE_NAME_LEN IFNAMSIZ

sai_status_t vs_recv_hostif_packet(
        _In_ sai_object_id_t hif_id,
        _Inout_ sai_size_t *buffer_size,
        _Out_ void *buffer,
        _Inout_ uint32_t *attr_count,
        _Out_ sai_attribute_t *attr_list)
{
    MUTEX();
    SWSS_LOG_ENTER();
    VS_CHECK_API_INITIALIZED();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t vs_send_hostif_packet(
        _In_ sai_object_id_t hif_id,
        _In_ sai_size_t buffer_size,
        _In_ const void *buffer,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    MUTEX();
    SWSS_LOG_ENTER();
    VS_CHECK_API_INITIALIZED();

    return SAI_STATUS_NOT_IMPLEMENTED;
}

int vs_create_tap_device(
        _In_ const char *dev,
        _In_ int flags)
{
    SWSS_LOG_ENTER();

    const char *tundev = "/dev/net/tun";

    int fd = open(tundev, O_RDWR);

    if (fd < 0)
    {
        SWSS_LOG_ERROR("failed to open %s", tundev);

        return -1;
    }

    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = (short int)flags;  // IFF_TUN or IFF_TAP, IFF_NO_PI

    strncpy(ifr.ifr_name, dev, IFNAMSIZ-1);

    int err = ioctl(fd, TUNSETIFF, (void *) &ifr);

    if (err < 0)
    {
        SWSS_LOG_ERROR("ioctl TUNSETIFF on fd %d %s failed, err %d", fd, dev, err);

        close(fd);

        return err;
    }

    return fd;
}

int vs_set_dev_mac_address(
        _In_ const char *dev,
        _In_ const sai_mac_t& mac)
{
    SWSS_LOG_ENTER();

    int s = socket(AF_INET, SOCK_DGRAM, 0);

    if (s < 0)
    {
        SWSS_LOG_ERROR("failed to create socket, errno: %d", errno);

        return -1;
    }

    struct ifreq ifr;

    strncpy(ifr.ifr_name, dev, IFNAMSIZ-1);

    memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);

    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;

    int err = ioctl(s, SIOCSIFHWADDR, &ifr);

    if (err < 0)
    {
        SWSS_LOG_ERROR("ioctl SIOCSIFHWADDR on socket %d %s failed, err %d", s, dev, err);
    }

    close(s);

    return err;
}

/**
 * @brief Get SwitchState by switch id.
 *
 * Function will get shared object for switch state.  This function is thread
 * safe and it's only intended to use inside threads.
 *
 * @param switch_id Switch ID
 *
 * @return SwitchState object or null ptr if not found.
 */
std::shared_ptr<SwitchState> vs_get_switch_state(
        _In_ sai_object_id_t switch_id)
{
    MUTEX();

    SWSS_LOG_ENTER();

    auto it = g_switch_state_map.find(switch_id);

    if (it == g_switch_state_map.end())
    {
        return nullptr;
    }

    return it->second;
}

void update_port_oper_status(
        _In_ sai_object_id_t port_id,
        _In_ sai_port_oper_status_t port_oper_status)
{
    MUTEX();

    SWSS_LOG_ENTER();

    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_OPER_STATUS;
    attr.value.s32 = port_oper_status;

    sai_status_t status = vs_generic_set(SAI_OBJECT_TYPE_PORT, port_id, &attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to update port status %s: %s",
                sai_serialize_object_id(port_id).c_str(),
                sai_serialize_port_oper_status(port_oper_status).c_str());
    }
}

void send_port_up_notification(
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    sai_object_id_t switch_id = g_realObjectIdManager->saiSwitchIdQuery(port_id);

    if (switch_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("failed to get switch OID from port id %s",
                sai_serialize_object_id(port_id).c_str());
        return;
    }

    std::shared_ptr<SwitchState> sw = vs_get_switch_state(switch_id);

    if (sw == nullptr)
    {
        SWSS_LOG_ERROR("failed to get switch state for switch id %s",
                sai_serialize_object_id(switch_id).c_str());
        return;
    }

    sai_attribute_t attr;

    attr.id = SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY;

    if (vs_switch_api.get_switch_attribute(switch_id, 1, &attr) != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to get SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY for switch %s",
                sai_serialize_object_id(switch_id).c_str());
        return;
    }

    sai_port_state_change_notification_fn callback =
        (sai_port_state_change_notification_fn)attr.value.ptr;

    if (callback == NULL)
    {
        SWSS_LOG_INFO("SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY callback is NULL");
        return;
    }

    sai_port_oper_status_notification_t data;

    data.port_id = port_id;
    data.port_state = SAI_PORT_OPER_STATUS_UP;

    attr.id = SAI_PORT_ATTR_OPER_STATUS;

    update_port_oper_status(port_id, data.port_state);

    SWSS_LOG_NOTICE("explicitly send SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY for port %s: %s (port was UP)",
            sai_serialize_object_id(data.port_id).c_str(),
            sai_serialize_port_oper_status(data.port_state).c_str());

    // NOTE this callback should be executed from separate non blocking thread

    if (callback)
        callback(1, &data);
}

int ifup(
        _In_ const char *dev,
        _In_ sai_object_id_t port_id,
        _In_ bool up,
        _In_ bool explicitNotification)
{
    SWSS_LOG_ENTER();

    int s = socket(AF_INET, SOCK_DGRAM, 0);

    if (s < 0)
    {
        SWSS_LOG_ERROR("failed to open socket: %d", s);

        return -1;
    }

    struct ifreq ifr;

    memset(&ifr, 0, sizeof ifr);

    strncpy(ifr.ifr_name, dev , IFNAMSIZ-1);

    int err = ioctl(s, SIOCGIFFLAGS, &ifr);

    if (err < 0)
    {
        SWSS_LOG_ERROR("ioctl SIOCGIFFLAGS on socket %d %s failed, err %d", s, dev, err);

        close(s);

        return err;
    }

    if (up && explicitNotification && (ifr.ifr_flags & IFF_UP))
    {
        close(s);

        // interface status didn't changed, we need to send manual notification
        // that interface status is UP but that notification would need to be
        // sent after actual interface creation, since user may receive that
        // notification before hostif create function will actually return,
        // this can happen when syncd will be operating in synchronous mode

        send_port_up_notification(port_id);

        return 0;
    }

    if (up)
    {
        ifr.ifr_flags |= IFF_UP;
    }
    else
    {
        ifr.ifr_flags &= ~IFF_UP;
    }

    err = ioctl(s, SIOCSIFFLAGS, &ifr);

    if (err < 0)
    {
        SWSS_LOG_ERROR("ioctl SIOCSIFFLAGS on socket %d %s failed, err %d", s, dev, err);
    }

    close(s);

    return err;
}

int promisc(const char *dev)
{
    SWSS_LOG_ENTER();

    int s = socket(AF_INET, SOCK_DGRAM, 0);

    if (s < 0)
    {
        SWSS_LOG_ERROR("failed to open socket: %d", s);

        return -1;
    }

    struct ifreq ifr;

    memset(&ifr, 0, sizeof ifr);

    strncpy(ifr.ifr_name, dev , IFNAMSIZ-1);

    int err = ioctl(s, SIOCGIFFLAGS, &ifr);

    if (err < 0)
    {
        SWSS_LOG_ERROR("ioctl SIOCGIFFLAGS on socket %d %s failed, err %d", s, dev, err);

        close(s);

        return err;
    }

    if (ifr.ifr_flags & IFF_PROMISC)
    {
        close(s);

        return 0;
    }

    ifr.ifr_flags |= IFF_PROMISC;

    err = ioctl(s, SIOCSIFFLAGS, &ifr);

    if (err < 0)
    {
        SWSS_LOG_ERROR("ioctl SIOCSIFFLAGS on socket %d %s failed, err %d", s, dev, err);
    }

    close(s);

    return err;
}

int vs_set_dev_mtu(
        _In_ const char*name,
        _In_ int mtu)
{
    SWSS_LOG_ENTER();

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    struct ifreq ifr;

    strncpy(ifr.ifr_name, name, IFNAMSIZ-1);

    ifr.ifr_mtu = mtu;

    int err = ioctl(sock, SIOCSIFMTU, &ifr);

    if (err == 0)
    {
        SWSS_LOG_INFO("success set mtu on %s to %d", name, mtu);
        return 0;
    }

    SWSS_LOG_WARN("failed to set mtu on %s to %d", name, mtu);
    return err;
}


std::string vs_get_veth_name(
        _In_ const std::string& tapname,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    std::string vethname = SAI_VS_VETH_PREFIX + tapname;

    // check if user override interface names

    sai_attribute_t attr;

    uint32_t lanes[4];

    attr.id = SAI_PORT_ATTR_HW_LANE_LIST;

    attr.value.u32list.count = 4;
    attr.value.u32list.list = lanes;

    if (vs_generic_get(SAI_OBJECT_TYPE_PORT, port_id, 1, &attr) != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_WARN("failed to get port %s lanes, using veth: %s",
                sai_serialize_object_id(port_id).c_str(),
                vethname.c_str());
    }
    else
    {
        // TODO use actual map instead
        auto map = g_laneMapContainer->getLaneMap(0); // TODO use index

        auto ifname = map->getInterfaceFromLaneNumber(lanes[0]);

        if (ifname == "")
        {
            SWSS_LOG_WARN("failed to get ifname from lane number %u", lanes[0]);
        }
        else
        {
            SWSS_LOG_NOTICE("using %s instead of %s", ifname.c_str(), vethname.c_str());

            vethname = ifname;
        }
    }

    return vethname;
}

// TODO must be per switch when multiple switch support will be used
// since interface names can be the same in each switch
std::map<std::string, std::shared_ptr<HostInterfaceInfo>> g_hostif_info_map;

bool hostif_create_tap_veth_forwarding(
        _In_ const std::string &tapname,
        _In_ int tapfd,
        _In_ sai_object_id_t port_id)
{
    SWSS_LOG_ENTER();

    // we assume here that veth devices were added by user before creating this
    // host interface, vEthernetX will be used for packet transfer between ip
    // namespaces or ethernet device name used in lane map if provided

    std::string vethname = vs_get_veth_name(tapname, port_id);

    int packet_socket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

    if (packet_socket < 0)
    {
        SWSS_LOG_ERROR("failed to open packet socket, errno: %d", errno);

        return false;
    }

    int val = 1;
    if (setsockopt(packet_socket, SOL_PACKET, PACKET_AUXDATA, &val, sizeof(val)) < 0)
    {
        SWSS_LOG_ERROR("setsockopt() set PACKET_AUXDATA failed: %s", strerror(errno));
        return false;
    }

    // bind to device

    struct sockaddr_ll sock_address;

    memset(&sock_address, 0, sizeof(sock_address));

    sock_address.sll_family = PF_PACKET;
    sock_address.sll_protocol = htons(ETH_P_ALL);
    sock_address.sll_ifindex = if_nametoindex(vethname.c_str());

    if (sock_address.sll_ifindex == 0)
    {
        SWSS_LOG_ERROR("failed to get interface index for %s", vethname.c_str());

        close(packet_socket);

        return false;
    }

    // TODO we should listen only to those interfaces indexes on link message

    SWSS_LOG_NOTICE("interface index = %d, %s\n", sock_address.sll_ifindex, vethname.c_str());

    if (ifup(vethname.c_str(), port_id, true, true))
    {
        SWSS_LOG_ERROR("ifup failed on %s", vethname.c_str());

        close(packet_socket);

        return false;
    }

    if (promisc(vethname.c_str()))
    {
        SWSS_LOG_ERROR("promisc failed on %s", vethname.c_str());

        close(packet_socket);

        return false;
    }

    if (bind(packet_socket, (struct sockaddr*) &sock_address, sizeof(sock_address)) < 0)
    {
        SWSS_LOG_ERROR("bind failed on %s", vethname.c_str());

        close(packet_socket);

        return false;
    }

    g_hostif_info_map[tapname] =
        std::make_shared<HostInterfaceInfo>(0, packet_socket, tapfd, tapname, port_id);

    SWSS_LOG_NOTICE("setup forward rule for %s succeeded", tapname.c_str());

    return true;
}

sai_status_t vs_create_hostif_tap_interface(
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    // validate SAI_HOSTIF_ATTR_TYPE

    auto attr_type = sai_metadata_get_attr_by_id(SAI_HOSTIF_ATTR_TYPE, attr_count, attr_list);

    if (attr_type == NULL)
    {
        SWSS_LOG_ERROR("attr SAI_HOSTIF_ATTR_TYPE was not passed");

        return SAI_STATUS_FAILURE;
    }

    /* The genetlink host interface is created to associate trap group to genetlink family and multicast group
     * created by driver. It does not create any netdev interface. Hence skipping tap interface creation
     */
    if (attr_type->value.s32 == SAI_HOSTIF_TYPE_GENETLINK)
    {
        SWSS_LOG_DEBUG("Skipping tap create for hostif type genetlink");

        return SAI_STATUS_SUCCESS;
    }

    if (attr_type->value.s32 != SAI_HOSTIF_TYPE_NETDEV)
    {
        SWSS_LOG_ERROR("only SAI_HOSTIF_TYPE_NETDEV is supported");

        return SAI_STATUS_FAILURE;
    }

    // validate SAI_HOSTIF_ATTR_OBJ_ID

    auto attr_obj_id = sai_metadata_get_attr_by_id(SAI_HOSTIF_ATTR_OBJ_ID, attr_count, attr_list);

    if (attr_obj_id == NULL)
    {
        SWSS_LOG_ERROR("attr SAI_HOSTIF_ATTR_OBJ_ID was not passed");

        return SAI_STATUS_FAILURE;
    }

    sai_object_id_t obj_id = attr_obj_id->value.oid;

    sai_object_type_t ot = g_realObjectIdManager->saiObjectTypeQuery(obj_id);

    if (ot != SAI_OBJECT_TYPE_PORT)
    {
        SWSS_LOG_ERROR("SAI_HOSTIF_ATTR_OBJ_ID=%s expected to be PORT but is: %s",
                sai_serialize_object_id(obj_id).c_str(),
                sai_serialize_object_type(ot).c_str());

        return SAI_STATUS_FAILURE;
    }

    // validate SAI_HOSTIF_ATTR_NAME

    auto attr_name = sai_metadata_get_attr_by_id(SAI_HOSTIF_ATTR_NAME, attr_count, attr_list);

    if (attr_name == NULL)
    {
        SWSS_LOG_ERROR("attr SAI_HOSTIF_ATTR_NAME was not passed");

        return SAI_STATUS_FAILURE;
    }

    if (strnlen(attr_name->value.chardata, sizeof(attr_name->value.chardata)) >= MAX_INTERFACE_NAME_LEN)
    {
        SWSS_LOG_ERROR("interface name is too long: %.*s", MAX_INTERFACE_NAME_LEN, attr_name->value.chardata);

        return SAI_STATUS_FAILURE;
    }

    std::string name = std::string(attr_name->value.chardata);

    // create TAP device

    SWSS_LOG_INFO("creating hostif %s", name.c_str());

    int tapfd = vs_create_tap_device(name.c_str(), IFF_TAP | IFF_MULTI_QUEUE | IFF_NO_PI);

    if (tapfd < 0)
    {
        SWSS_LOG_ERROR("failed to create TAP device for %s", name.c_str());

        return SAI_STATUS_FAILURE;
    }

    SWSS_LOG_INFO("created TAP device for %s, fd: %d", name.c_str(), tapfd);

    // TODO currently tapfd is ignored, it should be closed on hostif_remove
    // and we should use it to read/write packets from that interface

    sai_attribute_t attr;

    memset(&attr, 0, sizeof(attr));

    attr.id = SAI_SWITCH_ATTR_SRC_MAC_ADDRESS;

    sai_status_t status = vs_generic_get(SAI_OBJECT_TYPE_SWITCH, switch_id, 1, &attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to get SAI_SWITCH_ATTR_SRC_MAC_ADDRESS on switch %s: %s",
                sai_serialize_object_id(switch_id).c_str(),
                sai_serialize_status(status).c_str());
    }

    int err = vs_set_dev_mac_address(name.c_str(), attr.value.mac);

    if (err < 0)
    {
        SWSS_LOG_ERROR("failed to set MAC address %s for %s",
                sai_serialize_mac(attr.value.mac).c_str(),
                name.c_str());

        close(tapfd);

        return SAI_STATUS_FAILURE;
    }

    vs_set_dev_mtu(name.c_str(), ETH_FRAME_BUFFER_SIZE);

    if (!hostif_create_tap_veth_forwarding(name, tapfd, obj_id))
    {
        SWSS_LOG_ERROR("forwarding rule on %s was not added", name.c_str());
    }

    std::string vname = vs_get_veth_name(name, obj_id);

    SWSS_LOG_INFO("mapping interface %s to port id %s",
            vname.c_str(),
            sai_serialize_object_id(obj_id).c_str());

    g_switch_state_map.at(switch_id)->setIfNameToPortId(vname, obj_id);
    g_switch_state_map.at(switch_id)->setPortIdToTapName(obj_id, name);

    // TODO what about FDB entries notifications, they also should
    // be generated if new mac address will show up on the interface/arp table

    // TODO IP address should be assigned when router interface is created

    SWSS_LOG_INFO("created tap interface %s", name.c_str());

    return SAI_STATUS_SUCCESS;
}

sai_status_t vs_recreate_hostif_tap_interfaces(
        _In_ sai_object_id_t switch_id)
{
    SWSS_LOG_ENTER();

    if (g_vs_hostif_use_tap_device == false)
    {
        return SAI_STATUS_SUCCESS;
    }

    if (g_switch_state_map.find(switch_id) == g_switch_state_map.end())
    {
        SWSS_LOG_ERROR("failed to find switch %s in switch state map", sai_serialize_object_id(switch_id).c_str());

        return SAI_STATUS_FAILURE;
    }

    auto &objectHash = g_switch_state_map.at(switch_id)->m_objectHash.at(SAI_OBJECT_TYPE_HOSTIF);

    SWSS_LOG_NOTICE("attempt to recreate %zu tap devices for host interfaces", objectHash.size());

    for (auto okvp: objectHash)
    {
        std::vector<sai_attribute_t> attrs;

        for (auto akvp: okvp.second)
        {
            attrs.push_back(*akvp.second->getAttr());
        }

        vs_create_hostif_tap_interface(switch_id, (uint32_t)attrs.size(), attrs.data());
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t vs_remove_hostif_tap_interface(
        _In_ sai_object_id_t hostif_id)
{
    SWSS_LOG_ENTER();

    // get tap interface name

    sai_object_id_t switch_id = g_realObjectIdManager->saiSwitchIdQuery(hostif_id);

    if (switch_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("failed to obtain switch_id from hostif_id %s",
                sai_serialize_object_id(hostif_id).c_str());

        return SAI_STATUS_FAILURE;
    }

    sai_attribute_t attr;

    attr.id = SAI_HOSTIF_ATTR_NAME;

    sai_status_t status = vs_generic_get(SAI_OBJECT_TYPE_HOSTIF, hostif_id, 1, &attr);

    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("failed to get attr name for hostif %s",
                sai_serialize_object_id(hostif_id).c_str());

        return status;
    }

    if (strnlen(attr.value.chardata, sizeof(attr.value.chardata)) >= MAX_INTERFACE_NAME_LEN)
    {
        SWSS_LOG_ERROR("interface name is too long: %.*s", MAX_INTERFACE_NAME_LEN, attr.value.chardata);

        return SAI_STATUS_FAILURE;
    }

    std::string name = std::string(attr.value.chardata);

    auto it = g_hostif_info_map.find(name);

    if (it == g_hostif_info_map.end())
    {
        SWSS_LOG_ERROR("failed to find host info entry for tap device: %s", name.c_str());

        return SAI_STATUS_FAILURE;
    }

    SWSS_LOG_NOTICE("attempting to remove tap device: %s", name.c_str());

    // TODO add hostif id into hostif info entry and search by this but id is
    // created after creating tap device

    auto info = it->second;

    // remove host info entry from map

    g_hostif_info_map.erase(it); // will stop threads

    // remove tap device

    int err = close(info->m_tapfd);

    if (err)
    {
        SWSS_LOG_ERROR("failed to remove tap device: %s, err: %d", name.c_str(), err);
    }

    // remove interface mapping

    std::string vname = vs_get_veth_name(name, info->m_portId);

    g_switch_state_map.at(switch_id)->removeIfNameToPortId(vname);
    g_switch_state_map.at(switch_id)->removePortIdToTapName(info->m_portId);

    SWSS_LOG_NOTICE("successfully removed hostif tap device: %s", name.c_str());

    return SAI_STATUS_SUCCESS;
}

sai_status_t vs_create_hostif_int(
        _In_ sai_object_type_t object_type,
        _Out_ sai_object_id_t *hostif_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    if (g_vs_hostif_use_tap_device == true)
    {
        sai_status_t status = vs_create_hostif_tap_interface(
                switch_id,
                attr_count,
                attr_list);

        if (status != SAI_STATUS_SUCCESS)
        {
            return status;
        }
    }

    return vs_generic_create(
            object_type,
            hostif_id,
            switch_id,
            attr_count,
            attr_list);
}

sai_status_t vs_create_hostif(
        _Out_ sai_object_id_t *hostif_id,
        _In_ sai_object_id_t switch_id,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    MUTEX();

    SWSS_LOG_ENTER();

    return meta_sai_create_oid(
            SAI_OBJECT_TYPE_HOSTIF,
            hostif_id,
            switch_id,
            attr_count,
            attr_list,
            &vs_create_hostif_int);
}

sai_status_t vs_set_admin_state(
            _In_ sai_object_id_t portId,
            _In_ bool up)
{
    SWSS_LOG_ENTER();

    // find corresponding host if interface and bring it down !
    for (auto& kvp: hostif_info_map)
    {
        auto tapname = kvp.first;

        if (kvp.second->portid == portId)
        {
            std::string vethname = vs_get_veth_name(tapname, portId);

            if (ifup(vethname.c_str(), portId, up, false))
            {
                SWSS_LOG_ERROR("if admin %s failed on %s failed: %s", (up ? "UP" : "DOWN"), vethname.c_str(),  strerror(errno));

                return SAI_STATUS_FAILURE;
            }

            SWSS_LOG_NOTICE("if admin %s success on %s", (up ? "UP" : "DOWN"), vethname.c_str());

            break;
        }
    }

    return SAI_STATUS_SUCCESS;
}

// TODO set must also be supported when we change operational status up/down
// and probably also generate notification then

sai_status_t vs_remove_hostif_int(
        _In_ sai_object_type_t object_type,
        _In_ sai_object_id_t hostif_id)
{
    SWSS_LOG_ENTER();

    if (g_vs_hostif_use_tap_device == true)
    {
        sai_status_t status = vs_remove_hostif_tap_interface(
                hostif_id);

        if (status != SAI_STATUS_SUCCESS)
        {
            return status;
        }
    }

    return vs_generic_remove(
            SAI_OBJECT_TYPE_HOSTIF,
            hostif_id);
}

sai_status_t vs_remove_hostif(
        _In_ sai_object_id_t hostif_id)
{
    MUTEX();

    SWSS_LOG_ENTER();

    return meta_sai_remove_oid(
            SAI_OBJECT_TYPE_HOSTIF,
            hostif_id,
            &vs_remove_hostif_int);
}

VS_SET(HOSTIF,hostif);
VS_GET(HOSTIF,hostif);

VS_GENERIC_QUAD(HOSTIF_TABLE_ENTRY,hostif_table_entry);
VS_GENERIC_QUAD(HOSTIF_TRAP_GROUP,hostif_trap_group);
VS_GENERIC_QUAD(HOSTIF_TRAP,hostif_trap);
VS_GENERIC_QUAD(HOSTIF_USER_DEFINED_TRAP,hostif_user_defined_trap);

const sai_hostif_api_t vs_hostif_api = {

    VS_GENERIC_QUAD_API(hostif)
    VS_GENERIC_QUAD_API(hostif_table_entry)
    VS_GENERIC_QUAD_API(hostif_trap_group)
    VS_GENERIC_QUAD_API(hostif_trap)
    VS_GENERIC_QUAD_API(hostif_user_defined_trap)

    vs_recv_hostif_packet,
    vs_send_hostif_packet,
};
