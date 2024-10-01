/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Zigbee HA_thermostat Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "esp_zigbee_core.h"
/* Zigbee configuration */
#define MAX_CHILDREN 10 /* the max amount of connected devices */
#define INSTALLCODE_POLICY_ENABLE                                              \
  false /* enable the install code policy for security */
#define HA_THERMOSTAT_ENDPOINT 1 /* esp thermostat device endpoint */
#define ESP_ZB_PRIMARY_CHANNEL_MASK                                            \
  (1 << 25) /* Zigbee primary channel mask use in the example */
#define ESP_ZB_SECONDARY_CHANNEL_MASK                                          \
  ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK /* Zigbee primary channel mask use in   \
                                          the example */

/* Attribute values in ZCL string format
 * The string should be started with the length of its own.
 */
#define MANUFACTURER_NAME                                                      \
  "\x07"                                                                       \
  "susch19"
#define MODEL_IDENTIFIER "\x06""heater"

#define ESP_ZB_ZC_CONFIG()                                                     \
  {                                                                            \
    .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,                                  \
    .install_code_policy = INSTALLCODE_POLICY_ENABLE,                          \
    .nwk_cfg = {.zczr_cfg = {                                                  \
                    .max_children = MAX_CHILDREN,                              \
                }},                                                            \
  }

/**
 * @brief Zigbee HA standard thermostat device default config value.
 *
 */
#define ESP_ZB_DEFAULT_THERMOSTAT_CONFIG_CPP()                                 \
  {                                                                            \
    .basic_cfg =                                                               \
        {                                                                      \
            .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,         \
            .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,       \
        },                                                                     \
    .identify_cfg =                                                            \
        {                                                                      \
            .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,  \
        },                                                                     \
    .thermostat_cfg = {                                                        \
        .local_temperature =                                                   \
            (int16_t)ESP_ZB_ZCL_THERMOSTAT_LOCAL_TEMPERATURE_DEFAULT_VALUE,    \
        .occupied_cooling_setpoint =                                           \
            ESP_ZB_ZCL_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_DEFAULT_VALUE,     \
        .occupied_heating_setpoint =                                           \
            ESP_ZB_ZCL_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_DEFAULT_VALUE,     \
        .control_sequence_of_operation =                                       \
            ESP_ZB_ZCL_THERMOSTAT_CONTROL_SEQ_OF_OPERATION_DEFAULT_VALUE,      \
        .system_mode =                                                         \
            ESP_ZB_ZCL_THERMOSTAT_CONTROL_SYSTEM_MODE_DEFAULT_VALUE,           \
    },                                                                         \
  }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                                          \
  { .radio_mode = ZB_RADIO_MODE_NATIVE, }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                                           \
  { .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, }

#ifdef __cplusplus
}

#endif