#pragma once

#include "swss/notificationproducer.h"

#include "VirtualOidTranslator.h"

extern std::shared_ptr<syncd::VirtualOidTranslator> g_translator; // TODO move to syncd object
extern std::shared_ptr<swss::NotificationProducer>  notifications;
extern std::shared_ptr<swss::DBConnector>   dbAsic;
extern std::string fdbFlushSha;
