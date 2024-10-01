#pragma once

#include "esp_ota.h"
#include "esp_zigbee_core.h"
#include "heater.hpp"
#include "storage.hpp"
#include "temperature_sensor.hpp"
#include <stdint.h>

#define CUSTOM_SERVER_ENDPOINT 0x01
#define CUSTOM_CLIENT_ENDPOINT 0x01
#define CUSTOM_COMMAND_RESP 0x01
#define SET_SETPOINT_COMMAND_ID 0x00
#define SET_WEEKLY_SCHEDULE_COMMAND_ID 0x01
#define GET_WEEKLY_SCHEDULE_COMMAND_ID 0x02
#define CLEAR_WEEKLY_SCHEDULE_COMMAND_ID 0x03
#define SET_CUSTOM_WEEKLY_SCHEDULE_COMMAND_ID 0xff

class ZigbeeDevice {

public:
  static ZigbeeDevice *GetInstance();

  ZigbeeDevice(ZigbeeDevice &other) = delete;
  void operator=(const ZigbeeDevice &) = delete;

protected:
  static ZigbeeDevice *_instance;
  ZigbeeDevice() {}

public:
  void init();
  esp_err_t actionHandler(esp_zb_core_action_callback_id_t callback_id,
                          const void *message);
  void esp_app_zb_attribute_handler(uint16_t cluster_id,
                                    const esp_zb_zcl_attribute_t *attribute);
  esp_err_t zb_custom_request_handler(
      const esp_zb_zcl_custom_cluster_command_message_t *message);
  esp_err_t zb_configure_report_resp_handler(
      const esp_zb_zcl_cmd_config_report_resp_message_t *message);
  esp_err_t zb_read_attr_resp_handler(
      const esp_zb_zcl_cmd_read_attr_resp_message_t *message);
  esp_err_t zb_attribute_reporting_handler(
      const esp_zb_zcl_report_attr_message_t *message);
  void addReportingToCoordinator(uint16_t clusterId, uint16_t attrId,
                                 esp_zb_zcl_cluster_role_t cluserRole);

  esp_err_t
  zb_attribute_set_handler(const esp_zb_zcl_set_attr_value_message_t *message);

  static float s16ToTemperature(int16_t value);
  static int16_t temperatureTos16(float temp);
  static void temperatureReceived(float *temp,
                                  const void *additionalParameters);

public:
  TemperatureSensor *tempSensor;
  CompressedOTA *ota;
  Heater *heater;
  Storage *storage;
};