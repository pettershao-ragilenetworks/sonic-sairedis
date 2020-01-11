#pragma once

extern "C" {
#include "sai.h"
}

#include "swss/sal.h"
#include "swss/netmsg.h"

namespace saivs
{
    class LinkMsg: 
        public swss::NetMsg
    {
        public:

            LinkMsg(
                    _In_ sai_object_id_t switch_id);

            virtual ~LinkMsg() = default;

        public:

            virtual void onMsg(
                    _In_ int nlmsg_type, 
                    _In_ struct nl_object *obj) override;
        private:

            sai_object_id_t m_switch_id;
    };
}
