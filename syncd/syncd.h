#ifndef __SYNCD_H__
#define __SYNCD_H__

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <thread>
#include <set>
#include <memory>

#include <unistd.h>
#include <execinfo.h>
#include <signal.h>

#ifdef SAITHRIFT
#include <utility>
#include <algorithm>
#include <switch_sai_rpc_server.h>
#endif // SAITHRIFT

#include "string.h"
extern "C" {
#include "sai.h"
}

#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"
#include "swss/redisclient.h"
#include "swss/dbconnector.h"
#include "swss/producertable.h"
#include "swss/consumertable.h"
#include "swss/consumerstatetable.h"
#include "swss/notificationconsumer.h"
#include "swss/notificationproducer.h"
#include "swss/selectableevent.h"
#include "swss/select.h"
#include "swss/logger.h"
#include "swss/table.h"

#include "SaiSwitch.h"
#include "VirtualOidTranslator.h"

#include "VendorSai.h"

#define UNREFERENCED_PARAMETER(X)

#define DEFAULT_VLAN_NUMBER         1

#define VIDTORID                    "VIDTORID"
#define RIDTOVID                    "RIDTOVID"
#define LANES                       "LANES"
#define HIDDEN                      "HIDDEN"
#define COLDVIDS                    "COLDVIDS"


#ifdef SAITHRIFT
#define SWITCH_SAI_THRIFT_RPC_SERVER_PORT 9092
#endif // SAITHRIFT

extern std::shared_ptr<sairedis::SaiInterface> g_vendorSai;

extern std::mutex g_mutex;

extern std::map<sai_object_id_t, std::shared_ptr<syncd::SaiSwitch>> switches;

extern std::shared_ptr<syncd::VirtualOidTranslator> g_translator; // TODO move to syncd object

void startDiagShell();

void hardReinit();

void performWarmRestart();

void redisClearVidToRidMap();
void redisClearRidToVidMap();

extern std::shared_ptr<swss::NotificationProducer>  notifications;
extern std::shared_ptr<swss::RedisClient>   g_redisClient;
extern std::shared_ptr<swss::DBConnector>   dbAsic;
extern std::string fdbFlushSha;

sai_status_t syncdApplyView();
void check_notifications_pointers(
        _In_ uint32_t attr_count,
        _In_ sai_attribute_t *attr_list);

bool is_set_attribute_workaround(
        _In_ sai_object_type_t objecttype,
        _In_ sai_attr_id_t attrid,
        _In_ sai_status_t status);

void startNotificationsProcessingThread();
void stopNotificationsProcessingThread();

sai_status_t processBulkEvent(
        _In_ sai_common_api_t api,
        _In_ const swss::KeyOpFieldsValuesTuple &kco);

void set_sai_api_loglevel();

void set_sai_api_log_min_prio(
        _In_ const std::string &prio);

#endif // __SYNCD_H__
