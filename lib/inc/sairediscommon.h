#pragma once

/*
 * This header will contain definitions used by libsairedis and syncd as well
 * as libswsscommon (for producer/consumer) and lua scripts.
 */

#define SYNCD_INIT_VIEW     "INIT_VIEW"
#define SYNCD_APPLY_VIEW    "APPLY_VIEW"
#define SYNCD_INSPECT_ASIC  "SYNCD_INSPECT_ASIC"
#define ASIC_STATE_TABLE    "ASIC_STATE"
#define TEMP_PREFIX         "TEMP_"

// Messages for processing queries from libsairedis to syncd
#define STRING_ATTR_ENUM_VALUES_CAPABILITY_QUERY        "attr_enum_values_capability_query"
#define STRING_ATTR_ENUM_VALUES_CAPABILITY_RESPONSE     "attr_enum_values_capability_response"
#define STRING_OBJECT_TYPE_GET_AVAILABILITY_QUERY       "object_type_get_availability_query"
#define STRING_OBJECT_TYPE_GET_AVAILABILITY_RESPONSE    "object_type_get_availability_response"

// TODO move this to SAI meta repository for auto generate

#define SAI_SWITCH_NOTIFICATION_NAME_BFD_SESSION_STATE_CHANGE   "bfd_session_state_change"
#define SAI_SWITCH_NOTIFICATION_NAME_FDB_EVENT                  "fdb_event"
#define SAI_SWITCH_NOTIFICATION_NAME_PACKET_EVENT               "packet_event"
#define SAI_SWITCH_NOTIFICATION_NAME_PORT_STATE_CHANGE          "port_state_change"
#define SAI_SWITCH_NOTIFICATION_NAME_QUEUE_PFC_DEADLOCK         "queue_deadlock"
#define SAI_SWITCH_NOTIFICATION_NAME_SWITCH_SHUTDOWN_REQUEST    "switch_shutdown_request"
#define SAI_SWITCH_NOTIFICATION_NAME_SWITCH_STATE_CHANGE        "switch_state_change"
#define SAI_SWITCH_NOTIFICATION_NAME_TAM_EVENT                  "tam_event"

/**
 * @brief Redis virtual object id counter key name.
 *
 * This key will be used by sairedis and syncd in REDIS database to generate
 * new object indexes used when constructing new virtual object id (VID).
 *
 * This key must have atomic access since it can be used at any time by syncd
 * process or orchagent process.
 */
#define REDIS_KEY_VIDCOUNTER "VIDCOUNTER"

/**
 * @brief Table which will be used to forward notifications from syncd.
 */
#define REDIS_TABLE_NOTIFICATIONS "NOTIFICATIONS"

