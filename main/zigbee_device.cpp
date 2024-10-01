#include "zigbee_device.hpp"
#include "clock.hpp"
#include "custom_cluster.hpp"
#include "esp_check.h"
#include "esp_zb_thermostat.hpp"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_core.h"
#include "heater.hpp"
#include "temperature_sensor.hpp"
#include "time.h"
#include "zcl/esp_zigbee_zcl_time.h"
#include <sys/select.h>

static const char *TAG = "ZIGBEE_DEVICE";

ZigbeeDevice *ZigbeeDevice::_instance = nullptr;

ZigbeeDevice *ZigbeeDevice::GetInstance() {
  if (_instance == nullptr) {
    _instance = new ZigbeeDevice();
  }
  return _instance;
}

float ZigbeeDevice::s16ToTemperature(int16_t value) {
  return 1.0 * value / 100;
}

int16_t ZigbeeDevice::temperatureTos16(float temp) {
  return (int16_t)(temp * 100);
}

void ZigbeeDevice::temperatureReceived(float *temp,
                                       const void *additionalParameters) {

  int16_t measured_value = temperatureTos16(*temp);
  ESP_LOGI(TAG, "Temp Rec: %f => %d", *temp, measured_value);
  /* Update temperature sensor measured value */
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_set_attribute_val(
      HA_THERMOSTAT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
      &measured_value, false);
  esp_zb_lock_release();
}

void ZigbeeDevice::init() {
  tempSensor = TemperatureSensor::GetInstance();
  tempSensor->init();
  tempSensor->addTempCallback(temperatureReceived, NULL);

  heater = Heater::GetInstance();
  heater->init();

  ota = new CompressedOTA();

  storage = Storage::GetInstance();
}
esp_err_t
ZigbeeDevice::actionHandler(esp_zb_core_action_callback_id_t callback_id,
                            const void *message) {
  esp_err_t ret = ESP_OK;
  ESP_LOGD(TAG, "Got a zigbee action %x", callback_id);
  switch (callback_id) {
  case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
    ret = ota->zbOTAUpgradeStatusHandler(
        (esp_zb_zcl_ota_upgrade_value_message_t *)message);
    break;
  case ESP_ZB_CORE_REPORT_ATTR_CB_ID:
    ret = zb_attribute_reporting_handler(
        (esp_zb_zcl_report_attr_message_t *)message);
    break;
  case ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID:
    ret = zb_read_attr_resp_handler(
        (esp_zb_zcl_cmd_read_attr_resp_message_t *)message);
    break;
  case ESP_ZB_CORE_CMD_REPORT_CONFIG_RESP_CB_ID:
    ret = zb_configure_report_resp_handler(
        (esp_zb_zcl_cmd_config_report_resp_message_t *)message);
    break;
  case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
    ret = zb_attribute_set_handler(
        (esp_zb_zcl_set_attr_value_message_t *)message);
    break;
  case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID:
    ret = zb_custom_request_handler(
        (esp_zb_zcl_custom_cluster_command_message_t *)message);
    break;
  case ESP_ZB_CORE_CMD_GREEN_POWER_RECV_CB_ID:
    break;
  default:
    ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
    break;
  }
  return ret;
}

void ZigbeeDevice::esp_app_zb_attribute_handler(
    uint16_t cluster_id, const esp_zb_zcl_attribute_t *attribute) {

  ESP_LOGI(TAG, "esp_app_zb_attribute_handler Cluster:%x Attribtue:%x",
           cluster_id, attribute->id);
 
  if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_TIME) {
    auto clock = Clock::GetInstance();
    if (attribute->id == ESP_ZB_ZCL_ATTR_TIME_TIME_ID &&
        attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_UTC_TIME) {

      uint32_t value =
          attribute->data.value ? *(uint32_t *)attribute->data.value : 0;

      clock->updateTime(value);
    } else if (attribute->id == ESP_ZB_ZCL_ATTR_TIME_LOCAL_TIME_ID &&
               attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_U32) {
      uint32_t value =
          attribute->data.value ? *(uint32_t *)attribute->data.value : 0;

      clock->updateTimeZone(value);
    }
  }
  
  if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT) {
    if (attribute->id == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID &&
        attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
      heater->updateRemoteTemp(
          attribute->data.value ? *(int16_t *)attribute->data.value : 0);
    }
  }

  if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT) {
    if (attribute->id == ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID) {
      heater->updateSystemMode(*(uint8_t *)attribute->data.value);
    } else if (attribute->id ==
               ESP_ZB_ZCL_ATTR_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT_ID) {
      heater->updateManualTemp(*(int16_t *)attribute->data.value);
    }
  }
}
esp_err_t ZigbeeDevice::zb_attribute_reporting_handler(
    const esp_zb_zcl_report_attr_message_t *message) {
  ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
  ESP_RETURN_ON_FALSE(message->status == ESP_ZB_ZCL_STATUS_SUCCESS,
                      ESP_ERR_INVALID_ARG, TAG,
                      "Received message: error status(%d)", message->status);
  ESP_LOGI(TAG,
           "Received report from address(0x%x) src endpoint(%d) to dst "
           "endpoint(%d) cluster(0x%x)",
           message->src_address.u.short_addr, message->src_endpoint,
           message->dst_endpoint, message->cluster);
  esp_app_zb_attribute_handler(message->cluster, &message->attribute);
  return ESP_OK;
}

esp_err_t ZigbeeDevice::zb_read_attr_resp_handler(
    const esp_zb_zcl_cmd_read_attr_resp_message_t *message) {
  ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
  ESP_RETURN_ON_FALSE(
      message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG,
      TAG, "Received message: error status(%d)", message->info.status);

  ESP_LOGI(TAG,
           "Read attribute response: from address(0x%x) src endpoint(%d) to "
           "dst endpoint(%d) cluster(0x%x)",
           message->info.src_address.u.short_addr, message->info.src_endpoint,
           message->info.dst_endpoint, message->info.cluster);

  esp_zb_zcl_read_attr_resp_variable_t *variable = message->variables;
  while (variable) {
    ESP_LOGI(TAG,
             "Read attribute response: status(%d), cluster(0x%x), "
             "attribute(0x%x), type(0x%x), value(%d)",
             variable->status, message->info.cluster, variable->attribute.id,
             variable->attribute.data.type,
             variable->attribute.data.value
                 ? *(uint8_t *)variable->attribute.data.value
                 : 0);
    if (variable->status == ESP_ZB_ZCL_STATUS_SUCCESS) {
      esp_app_zb_attribute_handler(message->info.cluster, &variable->attribute);
    }

    variable = variable->next;
  }

  return ESP_OK;
}

esp_err_t ZigbeeDevice::zb_configure_report_resp_handler(
    const esp_zb_zcl_cmd_config_report_resp_message_t *message) {
  ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
  ESP_RETURN_ON_FALSE(
      message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG,
      TAG, "Received message: error status(%d)", message->info.status);

  esp_zb_zcl_config_report_resp_variable_t *variable = message->variables;
  while (variable) {
    ESP_LOGI(TAG,
             "Configure report response: status(%d), cluster(0x%x), "
             "direction(0x%x), attribute(0x%x)",
             variable->status, message->info.cluster, variable->direction,
             variable->attribute_id);
    variable = variable->next;
  }

  return ESP_OK;
}

esp_err_t ZigbeeDevice::zb_attribute_set_handler(
    const esp_zb_zcl_set_attr_value_message_t *message) {
  esp_err_t ret = ESP_OK;

  ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
  ESP_RETURN_ON_FALSE(
      message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG,
      TAG, "Received message: error status(%d)", message->info.status);

  ESP_LOGI(
      TAG, "Receive attribute set: %d from address 0x%04hx to attribute %x",
      message->info.cluster, message->info.dst_endpoint, message->attribute.id);

  if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_TIME) {
    auto clock = Clock::GetInstance();
    if (message->attribute.id == ESP_ZB_ZCL_ATTR_TIME_TIME_ID &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_UTC_TIME) {

      uint32_t value = message->attribute.data.value
                           ? *(uint32_t *)message->attribute.data.value
                           : 0;

      clock->updateTime(value);
    } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_TIME_TIME_ZONE_ID) {
      int32_t value = message->attribute.data.value
                          ? *(int32_t *)message->attribute.data.value
                          : 0;

      clock->updateTimeZone(value);
    }
  } else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT) {
    if (message->attribute.id ==
            ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {

    } else if (message->attribute.id ==
               ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID) {
      heater->updateSystemMode(*(uint8_t *)message->attribute.data.value);
    } else if (message->attribute.id ==
                   ESP_ZB_ZCL_ATTR_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT_ID &&
               message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_S16) {
      heater->updateManualTemp(*(int16_t *)message->attribute.data.value);
    }
  } else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_CUSTOM) {
    if (message->attribute.id == ESP_ZB_ZCL_ATTR_CUSTOM_RUNTIME_SECONDS_ID &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U32) {
      heater->updateRuntime(*(uint32_t *)message->attribute.data.value);
    }
  }

  return ret;
}

esp_err_t ZigbeeDevice::zb_custom_request_handler(
    const esp_zb_zcl_custom_cluster_command_message_t *message) {
  esp_err_t ret = ESP_OK;

  ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
  ESP_RETURN_ON_FALSE(
      message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG,
      TAG, "Received message: error status(%d)", message->info.status);

  if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_CUSTOM ||
      message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT) {
    switch (message->info.command.id) {
    case SET_WEEKLY_SCHEDULE_COMMAND_ID: {
      ESP_LOG_BUFFER_HEX(TAG, ((uint8_t *)message->data.value),
                         message->data.size);

      esp_zb_weekly_schedule_header_t header =
          ((esp_zb_weekly_schedule_header_t *)message->data.value)[0];
      ESP_LOGI(TAG, "Length: %d, DayOfWeek: %x, Mode: %x",
               header.numberOfTransitions, header.dayOfWeekForSequence,
               header.mode);

      auto trimmedValue = ((uint8_t *)message->data.value) +
                          sizeof(esp_zb_weekly_schedule_header_t);
      auto settings = (esp_zb_weekly_schedule_single_s *)trimmedValue;
      auto heater = Heater::GetInstance();
      heater->updateSchedule(header, settings);
      heater->printSchedule();
      break;
    }
    case SET_CUSTOM_WEEKLY_SCHEDULE_COMMAND_ID: {
      auto header =
          *((esp_zb_custom_weekly_schedule_header_t *)message->data.value);

      auto trimmedValue = ((uint8_t *)message->data.value) +
                          sizeof(esp_zb_custom_weekly_schedule_header_t);
      auto settings = (esp_zb_custom_weekly_schedule_t *)trimmedValue;
            auto heater = Heater::GetInstance();
      heater->updateCustomSchedule(header, settings);
      heater->printSchedule();

      break;
    }
    case CLEAR_WEEKLY_SCHEDULE_COMMAND_ID: {
      auto clock = Clock::GetInstance();
      clock->syncTimeRequest();
      break;
    }
    default:
      break;
    }
  } else {

    ESP_LOGI(TAG, "Receive custom command: %d from address 0x%04hx",
             message->info.command.id, message->info.src_address.u.short_addr);
    ESP_LOGI(TAG, "Payload size: %d", message->data.size);
    ESP_LOG_BUFFER_CHAR(TAG, ((uint8_t *)message->data.value),
                        message->data.size);
  }

  return ret;
}

void ZigbeeDevice::addReportingToCoordinator(
    uint16_t clusterId, uint16_t attrId, esp_zb_zcl_cluster_role_t cluserRole) {

  esp_zb_zcl_reporting_info_t reporting_info = {
      .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
      .ep = HA_THERMOSTAT_ENDPOINT,
      .cluster_id = clusterId,
      .cluster_role = (uint8_t)cluserRole,
      .attr_id = attrId,
      .flags = 0,
      .run_time = 0,
      .u{
          .send_info = {.min_interval = 10,
                        .max_interval = 3600,
                        .delta = {},
                        .reported_value = {},
                        .def_min_interval = 10,
                        .def_max_interval = 3600},
      },
      .dst{
          .short_addr = 0,
          .endpoint = 0,
          .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
      },

      .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
  };
  esp_zb_zcl_update_reporting_info(&reporting_info);
}