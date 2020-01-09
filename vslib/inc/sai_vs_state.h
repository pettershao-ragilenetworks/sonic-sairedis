#ifndef __SAI_VS_STATE__
#define __SAI_VS_STATE__

#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

#include "swss/selectableevent.h"

#include "RealObjectIdManager.h"
#include "FdbInfo.h"
#include "SaiAttrWrap.h"

#include <unordered_map>
#include <string>
#include <set>

#define CHECK_STATUS(status)            \
    {                                   \
        sai_status_t s = (status);      \
        if (s != SAI_STATUS_SUCCESS)    \
            { return s; }               \
    }

/**
 * @brief AttrHash key is attribute ID, value is actual attribute
 */
typedef std::map<std::string, std::shared_ptr<saivs::SaiAttrWrap>> AttrHash;

/**
 * @brief ObjectHash is map indexed by object type and then serialized object id.
 */
typedef std::map<sai_object_type_t, std::map<std::string, AttrHash>> ObjectHash;

#define DEFAULT_VLAN_NUMBER 1

#define SAI_VS_FDB_INFO "SAI_VS_FDB_INFO"

extern std::set<saivs::FdbInfo> g_fdb_info_set;

class SwitchState
{
    public:

        SwitchState(
                _In_ sai_object_id_t switch_id):
            m_switch_id(switch_id)
        {
            SWSS_LOG_ENTER();

            if (g_realObjectIdManager->saiObjectTypeQuery(switch_id) != SAI_OBJECT_TYPE_SWITCH)
            {
                SWSS_LOG_THROW("object %s is not SWITCH, its %s",
                        sai_serialize_object_id(switch_id).c_str(),
                        sai_serialize_object_type(g_realObjectIdManager->saiObjectTypeQuery(switch_id)).c_str());
            }

            for (int i = SAI_OBJECT_TYPE_NULL; i < (int)SAI_OBJECT_TYPE_EXTENSIONS_MAX; ++i)
            {
                /*
                 * Populate empty maps for each object to avoid checking if
                 * objecttype exists.
                 */

                objectHash[(sai_object_type_t)i] = { };
            }

            /*
             * Create switch by default, it will require special treat on
             * creating.
             */

            objectHash[SAI_OBJECT_TYPE_SWITCH][sai_serialize_object_id(switch_id)] = {};
        }

    ObjectHash objectHash;

    std::map<std::string, std::map<int, uint64_t>> countersMap;

    sai_object_id_t getSwitchId() const
    {
        SWSS_LOG_ENTER();

        return m_switch_id;
    }

    bool getRunLinkThread() const
    {
        SWSS_LOG_ENTER();

        return m_run_link_thread;
    }

    void setRunLinkThread(
            _In_ bool run)
    {
        SWSS_LOG_ENTER();

        m_run_link_thread = run;
    }

    swss::SelectableEvent* getLinkThreadEvent()
    {
        SWSS_LOG_ENTER();

        return &m_link_thread_event;
    }

    void setLinkThread(
            _In_ std::shared_ptr<std::thread> thread)
    {
        SWSS_LOG_ENTER();

        m_link_thread = thread;
    }

    std::shared_ptr<std::thread> getLinkThread() const
    {
        SWSS_LOG_ENTER();

        return m_link_thread;
    }

    void setIfNameToPortId(
            _In_ const std::string& ifname,
            _In_ sai_object_id_t port_id)
    {
        SWSS_LOG_ENTER();

        m_ifname_to_port_id_map[ifname] = port_id;
    }


    void removeIfNameToPortId(
            _In_ const std::string& ifname)
    {
        SWSS_LOG_ENTER();

        m_ifname_to_port_id_map.erase(ifname);
    }

    sai_object_id_t getPortIdFromIfName(
        _In_ const std::string& ifname) const
    {
        SWSS_LOG_ENTER();

        auto it = m_ifname_to_port_id_map.find(ifname);

        if (it == m_ifname_to_port_id_map.end())
        {
            return SAI_NULL_OBJECT_ID;
        }

        return it->second;
    }

    void setPortIdToTapName(
            _In_ sai_object_id_t port_id,
            _In_ const std::string& tapname)
    {
        SWSS_LOG_ENTER();

        m_port_id_to_tapname[port_id] = tapname;
    }

    void removePortIdToTapName(
            _In_ sai_object_id_t port_id)
    {
        SWSS_LOG_ENTER();

        m_port_id_to_tapname.erase(port_id);
    }

    bool  getTapNameFromPortId(
            _In_ const sai_object_id_t port_id,
            _Out_ std::string& if_name)
    {
        SWSS_LOG_ENTER();

        if (m_port_id_to_tapname.find(port_id) != m_port_id_to_tapname.end())
        {
            if_name = m_port_id_to_tapname[port_id];
            return true;
        }
        return false;
    }

    private:

        sai_object_id_t m_switch_id;

        std::map<sai_object_id_t, std::string> m_port_id_to_tapname;

        swss::SelectableEvent m_link_thread_event;

        volatile bool m_run_link_thread;

        std::shared_ptr<std::thread> m_link_thread;

        std::map<std::string, sai_object_id_t> m_ifname_to_port_id_map;
};

typedef std::map<sai_object_id_t, std::shared_ptr<SwitchState>> SwitchStateMap;

extern SwitchStateMap g_switch_state_map;

sai_status_t vs_recreate_hostif_tap_interfaces(
        _In_ sai_object_id_t switch_id);

void processFdbInfo(
        _In_ const saivs::FdbInfo& fi,
        _In_ sai_fdb_event_t fdb_event);

void update_port_oper_status(
        _In_ sai_object_id_t port_id,
        _In_ sai_port_oper_status_t port_oper_status);

std::shared_ptr<SwitchState> vs_get_switch_state(
        _In_ sai_object_id_t switch_id);

#endif // __SAI_VS_STATE__
