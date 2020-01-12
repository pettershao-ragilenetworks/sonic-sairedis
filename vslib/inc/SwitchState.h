#pragma once

extern "C" {
#include "sai.h"
}

#include "SaiAttrWrap.h"
#include "swss/selectableevent.h"

#include <map>
#include <memory>
#include <thread>
#include <string>

namespace saivs
{
    class SwitchState
    {
        public:

            /**
             * @brief AttrHash key is attribute ID, value is actual attribute
             */
            typedef std::map<std::string, std::shared_ptr<SaiAttrWrap>> AttrHash;

            /**
             * @brief ObjectHash is map indexed by object type and then serialized object id.
             */
            typedef std::map<sai_object_type_t, std::map<std::string, AttrHash>> ObjectHash;

            typedef std::map<sai_object_id_t, std::shared_ptr<SwitchState>> SwitchStateMap;

        public:

            SwitchState(
                    _In_ sai_object_id_t switch_id);

            virtual ~SwitchState();

        public: // TODO make private

            ObjectHash m_objectHash;

            std::map<std::string, std::map<int, uint64_t>> m_countersMap;

        public:

            sai_object_id_t getSwitchId() const;

            bool getRunLinkThread() const;

            void setRunLinkThread(
                    _In_ bool run);

            swss::SelectableEvent* getLinkThreadEvent();

            void setLinkThread(
                    _In_ std::shared_ptr<std::thread> thread);

            std::shared_ptr<std::thread> getLinkThread() const;

            void setIfNameToPortId(
                    _In_ const std::string& ifname,
                    _In_ sai_object_id_t port_id);

            void removeIfNameToPortId(
                    _In_ const std::string& ifname);

            sai_object_id_t getPortIdFromIfName(
                    _In_ const std::string& ifname) const;

            void setPortIdToTapName(
                    _In_ sai_object_id_t port_id,
                    _In_ const std::string& tapname);

            void removePortIdToTapName(
                    _In_ sai_object_id_t port_id);

            bool  getTapNameFromPortId(
                    _In_ const sai_object_id_t port_id,
                    _Out_ std::string& if_name);

        protected:

            void removeNetlinkMessageListener();

        protected:

            sai_object_id_t m_switch_id;

        protected: // tap device related objects

            std::map<sai_object_id_t, std::string> m_port_id_to_tapname;

            swss::SelectableEvent m_link_thread_event;

            volatile bool m_run_link_thread;

            std::shared_ptr<std::thread> m_link_thread;

            std::map<std::string, sai_object_id_t> m_ifname_to_port_id_map;
    };
}
