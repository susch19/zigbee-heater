#pragma once
#include "esp_zigbee_core.h"

enum esp_zb_day_of_week_t : uint8_t {
  SUNDAY = 1 << 0,
  MONDAY = 1 << 1,
  TUESDAY = 1 << 2,
  WEDNESDAY = 1 << 3,
  THURSDAY = 1 << 4,
  FRIDAY = 1 << 5,
  SATURDAY = 1 << 6,
  AWAY_OR_VACATION = 1 << 7,
};

enum thermostat_weekly_schedule_mode_for_seq_t : uint8_t {
  HEAT = 0x01, /*!< Heat value */
  COOL = 0x02, /*!< Cool value */
  BOTH = 0x03, /*!< Both (Heat and Cool) value */
};

struct ESP_ZB_PACKED_STRUCT esp_zb_weekly_schedule_header_t {
  uint8_t numberOfTransitions;
  esp_zb_day_of_week_t dayOfWeekForSequence;
  thermostat_weekly_schedule_mode_for_seq_t mode;
};

struct ESP_ZB_PACKED_STRUCT esp_zb_weekly_schedule_single_s {
  uint16_t transition_time;
  int16_t tempSetPoint;
};

struct ESP_ZB_PACKED_STRUCT esp_zb_weekly_schedule_heat_cool_t {
  uint16_t transition_time;
  int16_t heatSetPoint;
  int16_t coolSetPoint;
};