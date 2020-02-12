#pragma once

#include "swss/redisclient.h"
#include "swss/dbconnector.h"
#include "swss/notificationproducer.h"

#include "SaiSwitch.h"
#include "VirtualOidTranslator.h"
#include "VendorSai.h"

#define VIDTORID                    "VIDTORID"
#define RIDTOVID                    "RIDTOVID"
#define LANES                       "LANES"
#define HIDDEN                      "HIDDEN"
#define COLDVIDS                    "COLDVIDS"

#ifdef SAITHRIFT
#define SWITCH_SAI_THRIFT_RPC_SERVER_PORT 9092
#endif // SAITHRIFT

extern std::shared_ptr<syncd::VirtualOidTranslator> g_translator; // TODO move to syncd object

void redisClearVidToRidMap();
void redisClearRidToVidMap();

extern std::shared_ptr<swss::NotificationProducer>  notifications;
extern std::shared_ptr<swss::RedisClient>   g_redisClient;
extern std::shared_ptr<swss::DBConnector>   dbAsic;
extern std::string fdbFlushSha;
