#pragma once

#include "custom_zigbee_types/schedule.hpp"
typedef enum {
  ESP_ZB_ZCL_ATTR_CUSTOM_RUNTIME_SECONDS_ID = 0x0000,
  ESP_ZB_ZCL_ATTR_CUSTOM_TEMPERATURE_SOURCE_ID = 0x0001,
  ESP_ZB_ZCL_ATTR_CUSTOM_CURRENT_SCHEDULE_ID = 0x0002
  

} esp_zb_zcl_custom_attr_t;

typedef enum {
  ESP_ZB_ZCL_CLUSTER_ID_CUSTOM = 0xff00
} esp_zb_zcl_cluster_custom_id_t;

typedef enum {
    ESP_ZB_ZCL_CUSTOM_TEMPERATURE_SOURCE_NONE           = 0x00, //When no local sensor is available and no remote temp has been received
    ESP_ZB_ZCL_CUSTOM_TEMPERATURE_SOURCE_LOCAL          = 0x01, 
    ESP_ZB_ZCL_CUSTOM_TEMPERATURE_SOURCE_REMOTE         = 0x02,
} esp_zb_zcl_custom_temperature_source_t;

struct ESP_ZB_PACKED_STRUCT esp_zb_custom_weekly_schedule_header_t {
  uint8_t numberOfTransitions;
  thermostat_weekly_schedule_mode_for_seq_t mode;
};

struct ESP_ZB_PACKED_STRUCT esp_zb_custom_weekly_schedule_t {
  esp_zb_day_of_week_t dayOfWeekForSequence;
  uint16_t transition_time;
  int16_t tempSetPoint;
};