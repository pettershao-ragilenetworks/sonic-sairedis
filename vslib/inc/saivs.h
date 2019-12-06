#pragma once

#define SAI_KEY_VS_SWITCH_TYPE              "SAI_VS_SWITCH_TYPE"

/**
 * @def SAI_KEY_VS_INTERFACE_LANE_MAP_FILE
 *
 * If specified in profile.ini it should point to eth interface to lane map.
 *
 * Example:
 * eth0:1,2,3,4
 * eth1:5,6,7,8
 *
 * TODO must support hardware info for multiple switches
 */
#define SAI_KEY_VS_INTERFACE_LANE_MAP_FILE  "SAI_VS_INTERFACE_LANE_MAP_FILE"

/**
 * @def SAI_KEY_VS_HOSTIF_USE_TAP_DEVICE
 *
 * Bool flag, (true/false). If set to true, then during create host interface
 * sai object also tap device will be created and mac address will be assigned.
 * For this operation root privileges will be required.
 *
 * By default this flag is set to false.
 */
#define SAI_KEY_VS_HOSTIF_USE_TAP_DEVICE      "SAI_VS_HOSTIF_USE_TAP_DEVICE"

#define SAI_VALUE_VS_SWITCH_TYPE_BCM56850     "SAI_VS_SWITCH_TYPE_BCM56850"
#define SAI_VALUE_VS_SWITCH_TYPE_MLNX2700     "SAI_VS_SWITCH_TYPE_MLNX2700"

/*
 * Values for SAI_KEY_BOOT_TYPE (defined in saiswitch.h)
 */

#define SAI_VALUE_VS_BOOT_TYPE_COLD "0"
#define SAI_VALUE_VS_BOOT_TYPE_WARM "1"
#define SAI_VALUE_VS_BOOT_TYPE_FAST "2"

/**
 * @def SAI_VS_UNITTEST_CHANNEL
 *
 * Notification channel for redis database.
 */
#define SAI_VS_UNITTEST_CHANNEL     "SAI_VS_UNITTEST_CHANNEL"

/**
 * @def SAI_VS_UNITTEST_SET_RO_OP
 *
 * Notification operation for "SET" READ_ONLY attribute.
 */
#define SAI_VS_UNITTEST_SET_RO_OP   "set_ro"

/**
 * @def SAI_VS_UNITTEST_SET_STATS
 *
 * Notification operation for "SET" stats on specific object.
 */
#define SAI_VS_UNITTEST_SET_STATS_OP      "set_stats"

/**
 * @def SAI_VS_UNITTEST_ENABLE
 *
 * Notification operation for enabling unittests.
 */
#define SAI_VS_UNITTEST_ENABLE_UNITTESTS  "enable_unittests"
