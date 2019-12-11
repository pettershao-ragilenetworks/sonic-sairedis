#pragma once

/*
 * This header will contain definitions used by libsairedis and syncd as well
 * as libswsscommon (for producer/consumer) and lua scripts.
 */

// TODO move this to SAI meta repository for auto generate

#define SAI_SWITCH_NOTIFICATION_NAME_BFD_SESSION_STATE_CHANGE   "bfd_session_state_change"
#define SAI_SWITCH_NOTIFICATION_NAME_FDB_EVENT                  "fdb_event"
#define SAI_SWITCH_NOTIFICATION_NAME_PACKET_EVENT               "packet_event"
#define SAI_SWITCH_NOTIFICATION_NAME_PORT_STATE_CHANGE          "port_state_change"
#define SAI_SWITCH_NOTIFICATION_NAME_QUEUE_PFC_DEADLOCK         "queue_deadlock"
#define SAI_SWITCH_NOTIFICATION_NAME_SWITCH_SHUTDOWN_REQUEST    "switch_shutdown_request"
#define SAI_SWITCH_NOTIFICATION_NAME_SWITCH_STATE_CHAENE        "switch_state_change"
#define SAI_SWITCH_NOTIFICATION_NAME_TAM_EVENT                  "tam_event"

/**
 * @brief Redis virtual object id counter key name.
 *
 * This key will be used by sairedis and syncd in REDIS database to generate
 * new object indexes used when construting new virtual object id (VID).
 *
 * This key must have atomic access since it can be used at any time by syncd
 * process or orchagent process.
 */
#define REDIS_KEY_VIDCOUNTER "VIDCOUNTER"

