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

#include "esp_zb_thermostat.hpp"
#include "clock.hpp"
#include "custom_cluster.hpp"
#include "zigbee_device.hpp"

#include "esp_ota.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_core.h"

#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_thermostat.h"
#include "zcl/esp_zigbee_zcl_time.h"

#include "freertos/task.h"
#include "nvs_flash.h"
#include "string.h"
#include "switch_driver.h"

#include <cstdint>
#include <sys/select.h>
#include "driver/gpio.h"

static const char *TAG = "THERMOSTAT";
#define ARRAY_LENTH(arr) (sizeof(arr) / sizeof(arr[0]))

#if defined ZB_ED_ROLE
#error Define ZB_COORDINATOR_ROLE in idf.py menuconfig to compile thermostat source code.
#endif

static ZigbeeDevice *device = ZigbeeDevice::GetInstance();

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
  uint32_t *p_sg_p = signal_struct->p_app_signal;
  esp_err_t err_status = signal_struct->esp_err_status;
  esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;
  
  switch (sig_type) {
  case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
    ESP_LOGI(TAG, "Initialize Zigbee stack");
    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
    break;
  case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
    if (err_status == ESP_OK) {
      ESP_LOGI(TAG, "Device started up in %s factory-reset mode",
               esp_zb_bdb_is_factory_new() ? "" : "non");
      if (esp_zb_bdb_is_factory_new()) {
        ESP_LOGI(TAG, "Start network formation");
        esp_zb_bdb_start_top_level_commissioning(
            ESP_ZB_BDB_MODE_NETWORK_STEERING);
      } else {
        ESP_LOGI(TAG, "Device rebooted");
        auto clock = Clock::GetInstance();
        clock->syncTimeRequest();
        Heater::loadLatestZigbeeAttributeValues();
      }
    } else {
      ESP_LOGE(TAG, "Failed to initialize Zigbee stack (status: %s)",
               esp_err_to_name(err_status));
    }
    break;
  case ESP_ZB_BDB_SIGNAL_STEERING:
    if (err_status == ESP_OK) {
      esp_zb_ieee_addr_t extended_pan_id;
      esp_zb_get_extended_pan_id(extended_pan_id);
      ESP_LOGI(TAG,
               "Joined network successfully (Extended PAN ID: "
               "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, "
               "Channel:%d, Short Address: 0x%04hx)",
               extended_pan_id[7], extended_pan_id[6], extended_pan_id[5],
               extended_pan_id[4], extended_pan_id[3], extended_pan_id[2],
               extended_pan_id[1], extended_pan_id[0], esp_zb_get_pan_id(),
               esp_zb_get_current_channel(), esp_zb_get_short_address());
      auto clock = Clock::GetInstance();
      clock->syncTimeRequest();
      Heater::loadLatestZigbeeAttributeValues();
    }
    break;
  case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
    break;

  default:
    ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
             esp_zb_zdo_signal_to_string(sig_type), sig_type,
             esp_err_to_name(err_status));
    break;
  }
}

static void
custom_thermostat_clusters_create(esp_zb_cluster_list_t *cluster_list,
                                  esp_zb_thermostat_cfg_t *thermostat) {

  esp_zb_attribute_list_t *basic_cluster =
      esp_zb_basic_cluster_create(&(thermostat->basic_cfg));
  ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(
      basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
      (void *)MANUFACTURER_NAME));
  ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(
      basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
      (void *)MODEL_IDENTIFIER));
  ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(
      cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

  ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(
      cluster_list, esp_zb_identify_cluster_create(&(thermostat->identify_cfg)),
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

  ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(
      cluster_list, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY),
      ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));

  esp_zb_attribute_list_t *time_cluster =
      esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_TIME);

  auto clock = Clock::GetInstance();

  esp_zb_cluster_add_attr(
      time_cluster, ESP_ZB_ZCL_CLUSTER_ID_TIME, ESP_ZB_ZCL_ATTR_TIME_TIME_ID,
      ESP_ZB_ZCL_ATTR_TYPE_UTC_TIME,
      ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
      &(clock->zb_time));
  esp_zb_cluster_add_attr(
      time_cluster, ESP_ZB_ZCL_CLUSTER_ID_TIME,
      ESP_ZB_ZCL_ATTR_TIME_TIME_STATUS_ID, ESP_ZB_ZCL_ATTR_TYPE_8BITMAP,
      ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &(clock->timeStatus));
  esp_zb_cluster_add_attr(
      time_cluster, ESP_ZB_ZCL_CLUSTER_ID_TIME,
      ESP_ZB_ZCL_ATTR_TIME_LOCAL_TIME_ID, ESP_ZB_ZCL_ATTR_TYPE_U32,
      ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &(clock->localTime));

  ESP_ERROR_CHECK(esp_zb_cluster_list_add_time_cluster(
      cluster_list, time_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

  esp_zb_attribute_list_t *thermostart_cluster =
      esp_zb_thermostat_cluster_create(&(thermostat->thermostat_cfg));

  auto heater = Heater::GetInstance();
  uint8_t runningMode = 0;
  esp_zb_cluster_add_attr(thermostart_cluster, ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                          ESP_ZB_ZCL_ATTR_THERMOSTAT_RUNNING_MODE_ID,
                          ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE |
                              ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                          &runningMode);

  esp_zb_cluster_add_attr(thermostart_cluster, ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                          ESP_ZB_ZCL_ATTR_THERMOSTAT_SETPOINT_CHANGE_SOURCE_ID,
                          ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE |
                              ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                          &(heater->setpointChangeSource));

  esp_zb_cluster_add_attr(
      thermostart_cluster, ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
      ESP_ZB_ZCL_ATTR_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT_ID,
      ESP_ZB_ZCL_ATTR_TYPE_S16,
      ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_SCENE |
          ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
      &(heater->manualTemp));

  ESP_ERROR_CHECK(esp_zb_cluster_list_add_thermostat_cluster(
      cluster_list, thermostart_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

  /* Add temperature measurement cluster for attribute reporting */

  esp_zb_temperature_meas_cluster_cfg_t sensor_cfg = {
      .measured_value = 2100, .min_value = 50, .max_value = 12500};

  ESP_ERROR_CHECK(esp_zb_cluster_list_add_temperature_meas_cluster(
      cluster_list, esp_zb_temperature_meas_cluster_create(NULL),
      ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
  ESP_ERROR_CHECK(esp_zb_cluster_list_add_temperature_meas_cluster(
      cluster_list, esp_zb_temperature_meas_cluster_create(&sensor_cfg),
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

  esp_zb_attribute_list_t *custom_cluster =
      esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_CUSTOM);

  esp_zb_custom_cluster_add_custom_attr(
      custom_cluster, ESP_ZB_ZCL_ATTR_CUSTOM_RUNTIME_SECONDS_ID,
      ESP_ZB_ZCL_ATTR_TYPE_U32,
      ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
      &(heater->runtime_in_seconds));

  esp_zb_custom_cluster_add_custom_attr(
      custom_cluster, ESP_ZB_ZCL_ATTR_CUSTOM_TEMPERATURE_SOURCE_ID,
      ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
      ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
      &(heater->temperatureSource));

  esp_zb_custom_cluster_add_custom_attr(
      custom_cluster, ESP_ZB_ZCL_ATTR_CUSTOM_CURRENT_SCHEDULE_ID,
      ESP_ZB_ZCL_ATTR_TYPE_U32,
      ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
      &(heater->currentTarget));

  esp_zb_cluster_list_add_custom_cluster(cluster_list, custom_cluster,
                                         ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

static void zb_ota_upgrade_ep_create(esp_zb_cluster_list_t *cluster_list

) {
  esp_zb_ota_cluster_cfg_t ota_cluster_cfg = {
      .ota_upgrade_file_version = OTA_UPGRADE_RUNNING_FILE_VERSION,
      .ota_upgrade_manufacturer = OTA_UPGRADE_MANUFACTURER,
      .ota_upgrade_image_type = OTA_UPGRADE_IMAGE_TYPE,
      .ota_upgrade_downloaded_file_ver = OTA_UPGRADE_DOWNLOADED_FILE_VERSION};
  esp_zb_attribute_list_t *ota_cluster =
      esp_zb_ota_cluster_create(&ota_cluster_cfg);
  esp_zb_zcl_ota_upgrade_client_variable_t variable_config = {
      .timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
      .hw_version = OTA_UPGRADE_HW_VERSION,
      .max_data_size = OTA_UPGRADE_MAX_DATA_SIZE,
  };
  uint16_t ota_upgrade_server_addr = 0xffff;
  uint8_t ota_upgrade_server_ep = 0xff;

  /* Added attributes */
  ESP_ERROR_CHECK(esp_zb_ota_cluster_add_attr(
      ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID,
      (void *)&variable_config));
  ESP_ERROR_CHECK(esp_zb_ota_cluster_add_attr(
      ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID,
      (void *)&ota_upgrade_server_addr));
  ESP_ERROR_CHECK(esp_zb_ota_cluster_add_attr(
      ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID,
      (void *)&ota_upgrade_server_ep));

  ESP_ERROR_CHECK(esp_zb_cluster_list_add_ota_cluster(
      cluster_list, ota_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
}

static void esp_zb_configure_reporting() {
  return;
  device->addReportingToCoordinator(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                                    ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  device->addReportingToCoordinator(ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
                                    ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  device->addReportingToCoordinator(
      ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
      ESP_ZB_ZCL_ATTR_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT_ID,
      ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

  device->addReportingToCoordinator(
      ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
      ESP_ZB_ZCL_ATTR_THERMOSTAT_SETPOINT_CHANGE_SOURCE_ID,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  device->addReportingToCoordinator(ESP_ZB_ZCL_CLUSTER_ID_CUSTOM,
                                    ESP_ZB_ZCL_ATTR_CUSTOM_RUNTIME_SECONDS_ID,
                                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  device->addReportingToCoordinator(
      ESP_ZB_ZCL_CLUSTER_ID_CUSTOM,
      ESP_ZB_ZCL_ATTR_CUSTOM_TEMPERATURE_SOURCE_ID,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}

static esp_err_t
esp_zb_action_handler(esp_zb_core_action_callback_id_t callback_id,
                      const void *message) {
  return device->actionHandler(callback_id, message);
}

static void esp_zb_task(void *pvParameters) {

  /* Initialize Zigbee stack */
  esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZC_CONFIG();
  esp_zb_init(&zb_nwk_cfg);

  esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
  esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
  /* Create customized thermostat endpoint */
  esp_zb_thermostat_cfg_t thermostat_cfg =
      ESP_ZB_DEFAULT_THERMOSTAT_CONFIG_CPP();

  Heater *heater = Heater::GetInstance();
  heater->thermostat_cluster.control_sequence_of_operation =
      ESP_ZB_ZCL_THERMOSTAT_CONTROL_SEQ_OF_OPERATION_HEATING_ONLY;
  heater->thermostat_cluster.occupied_heating_setpoint = 2100;
  thermostat_cfg.thermostat_cfg = heater->thermostat_cluster;

  custom_thermostat_clusters_create(cluster_list, &thermostat_cfg);
  zb_ota_upgrade_ep_create(cluster_list);

  esp_zb_endpoint_config_t endpoint_config = {
      .endpoint = HA_THERMOSTAT_ENDPOINT,
      .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
      .app_device_id = ESP_ZB_HA_THERMOSTAT_DEVICE_ID,
      .app_device_version = 0};
  ESP_ERROR_CHECK(
      esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config));

  /* Register the device */
  esp_zb_device_register(ep_list);

  esp_zb_core_action_handler_register(esp_zb_action_handler);
  esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
  esp_zb_set_secondary_network_channel_set(ESP_ZB_SECONDARY_CHANNEL_MASK);
  ESP_ERROR_CHECK(esp_zb_start(false));


  esp_zb_stack_main_loop();
}

static void resetStartCounter(void *arg) {
  ESP_LOGI(TAG, "Resetting start counter");

  uint8_t restartCounter = 0;
  Storage::GetInstance()->writeValue("restartCounter", restartCounter);
}

static esp_err_t esp_zb_power_save_init(void) {
  esp_err_t rc = ESP_OK;
#ifdef CONFIG_PM_ENABLE
  int cur_cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  esp_pm_config_t pm_config = {.max_freq_mhz = cur_cpu_freq_mhz,
                               .min_freq_mhz = 10,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
                               .light_sleep_enable = false
#endif
  };
  rc = esp_pm_configure(&pm_config);
#endif
  return rc;
}

extern "C" void app_main() {
  esp_zb_platform_config_t config = {
      .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
      .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
  };

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  auto storage = Storage::GetInstance();
  uint8_t restartCounter = 0;
  auto res = storage->readValue("restartCounter", &restartCounter);

  if (res == ESP_OK) {

    ESP_EARLY_LOGI(TAG, "%d restarts detected", restartCounter);
    if (restartCounter > 5) {
      {
        ESP_EARLY_LOGI(TAG, "Doing Factory Reset");
        restartCounter = 0;
        storage->writeValue("restartCounter", restartCounter);
        ESP_ERROR_CHECK(nvs_flash_erase());
        esp_zb_factory_reset();
      }
    }
  }

  restartCounter++;
  ESP_EARLY_LOGI(TAG, "Inc startup counter");
  storage->writeValue("restartCounter", restartCounter);

  const esp_timer_create_args_t timer = {
      .callback = resetStartCounter,
      .dispatch_method = ESP_TIMER_TASK,
  };
  
  auto clock = Clock::GetInstance();

  clock->init();
  device->init();

  ESP_ERROR_CHECK(esp_zb_power_save_init());
  esp_timer_handle_t handle;
  ESP_ERROR_CHECK(esp_timer_create(&timer, &handle));
  ESP_ERROR_CHECK(esp_timer_start_once(handle, 10 * 1000 * 1000));

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_zb_platform_config(&config));
  xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
