#include "heater.hpp"
#include "clock.hpp"
#include "custom_cluster.hpp"
#include "esp_zb_thermostat.hpp"
#include "storage.hpp"
#include "sys/time.h"
#include "temperature_sensor.hpp"
#include "time.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_meter_identification.h"
#include "zcl/esp_zigbee_zcl_thermostat.h"
#include "zigbee_device.hpp"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <sys/select.h>
#include <time.h>
#include <vector>

static const char *TAG = "HEATER";

void Heater::loadStoredState() {
  uint32_t runtimeInSeconds = 0;
  if (storage->readValue("heat_runtime", &runtimeInSeconds) == ESP_OK)
    this->runtime_in_seconds = runtimeInSeconds;

  uint8_t mode;
  if (storage->readValue("heat_mode", &mode) == ESP_OK)
    this->thermostat_cluster.system_mode = mode;

  int16_t temp = 0;
  uint32_t tempReceived = 0;
  if (storage->readValue("heater_mnlTemp", &temp) == ESP_OK) {
    this->manualTemp = temp;
    if (storage->readValue("heater_mnlTime", &tempReceived) == ESP_OK)
      this->manualModeRecv = {.tv_sec = tempReceived, .tv_usec = 0};
  }

  if (storage->readValue("heater_rmtTemp", &temp) == ESP_OK) {
    this->remoteTemp = temp;
    if (storage->readValue("heater_rmtTime", &tempReceived) == ESP_OK)
      this->remoteRecv = {.tv_sec = tempReceived, .tv_usec = 0};
  }
}

void Heater::init() {
  storage = Storage::GetInstance();
  clock = Clock::GetInstance();
  tempSensor = TemperatureSensor::GetInstance();
  this->loadStoredState();

  for (size_t i = 0; i < 8; i++) {
    char msg[12];
    sprintf(msg, "schedule_%x", (uint8_t)i);
    size_t len;
    auto res = storage->readValue<void>(msg, NULL, &len);
    if (res != ESP_OK)
      continue;
    void *data = malloc(len);

    res = storage->readValue<void>(msg, data, &len);
    size_t listSize = len / sizeof(esp_zb_weekly_schedule_single_s);

    esp_zb_weekly_schedule_single_s *des =
        (esp_zb_weekly_schedule_single_s *)data;
    DayOfWeekW dow = (DayOfWeekW)i;
    for (size_t o = 0; o < listSize; o++) {
      this->config[dow].push_back(des[o]);
    }

    free(data);
  }
  // printSchedule();
  initialized = true;

  auto tempSensor = TemperatureSensor::GetInstance();
  tempSensor->addTempCallback(measuredTemperature, this);

  gpio_config_t gpioConfig = {.pin_bit_mask = 1 << HEATER_GPIO_PIN,
                              .mode = GPIO_MODE_OUTPUT,
                              .pull_up_en = GPIO_PULLUP_DISABLE,
                              .pull_down_en = GPIO_PULLDOWN_ENABLE,
                              .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&gpioConfig);

  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(HEATER_GPIO_PIN, 0));

  xTaskCreate(checkScheduleTask, "Heater_main", 4096, NULL, 6, NULL);
}

void Heater::measuredTemperature(float *temp, const void *parameters) {
  Heater *_this = (Heater *)parameters;
  _this->localSensorTemp = ZigbeeDevice::temperatureTos16(*temp);
}

void Heater::updateCustomSchedule(esp_zb_custom_weekly_schedule_header_t header,
                                  esp_zb_custom_weekly_schedule_t *data) {
  ESP_LOGI(TAG, "Custom Header %d, %d. Size of Header %x, size of Data %x",
           header.numberOfTransitions, header.mode, sizeof(header),
           sizeof(esp_zb_custom_weekly_schedule_t));

  std::map<DayOfWeekW, std::vector<esp_zb_weekly_schedule_single_s>> newConfig;

  for (size_t i = 0; i < header.numberOfTransitions; i++) {
    auto conf = data[i];
    ESP_LOGI(TAG, "Custom Data %d: %x, %d, %d", i, conf.dayOfWeekForSequence,
             conf.transition_time, conf.tempSetPoint);
    for (size_t o = 0; o < 8; o++) {
      if ((conf.dayOfWeekForSequence & 1 << o) < 1)
        continue;
      auto dayOfWeek = (DayOfWeekW)o;
      esp_zb_weekly_schedule_single_s single = {
          .transition_time = conf.transition_time,
          .tempSetPoint = conf.tempSetPoint};
      newConfig[dayOfWeek].push_back(single);
    }
  }

  for (auto &&i : newConfig) {
    this->config[i.first] = newConfig[i.first];
  }
  auto storage = Storage::GetInstance();
  for (auto &&i : newConfig) {
    auto size =
        (size_t)(i.second.size() * sizeof(esp_zb_weekly_schedule_single_s));
    auto asd = i.second.data();
    char msg[12];
    sprintf(msg, "schedule_%x", (uint8_t)i.first);

    storage->writeValue<void>(msg, (const void *)asd, size);
  }
}

void Heater::updateSchedule(esp_zb_weekly_schedule_header_t header,
                            esp_zb_weekly_schedule_single_s *data) {
  esp_zb_custom_weekly_schedule_header_t convertedHeader = {
      .numberOfTransitions = header.numberOfTransitions, .mode = header.mode};
  esp_zb_custom_weekly_schedule_t convertedDatas[10];

  for (size_t i = 0; i < header.numberOfTransitions; i++) {
    auto conf = data[i];
    ESP_LOGI(TAG, "Custom Data %d: %d, %d", i, conf.transition_time,
             conf.tempSetPoint);
    convertedDatas[i] = {.dayOfWeekForSequence = header.dayOfWeekForSequence,
                         .transition_time = conf.transition_time,
                         .tempSetPoint = conf.tempSetPoint};
  }
  updateCustomSchedule(convertedHeader, convertedDatas);

  // std::map<DayOfWeekW, std::vector<esp_zb_weekly_schedule_singe_s>>
  // newConfig; std::vector<DayOfWeekW> relevantDays;

  // for (size_t i = 0; i < 8; i++) {
  //   if (header.dayOfWeekForSequence & 1 << i) {
  //     relevantDays.push_back((DayOfWeekW)i);
  //   }
  // }
  // for (size_t i = 0; i < header.numberOfTransitions; i++) {
  //   esp_zb_weekly_schedule_singe_s item = data[i];
  //   for (auto &&i : relevantDays) {
  //     newConfig[i].push_back(item);
  //   }
  // }
  // for (auto &&i : relevantDays) {
  //   this->config[i] = newConfig[i];
  // }
  // auto storage = Storage::GetInstance();
  // for (auto &&i : newConfig) {
  //   auto size =
  //       (size_t)(i.second.size() * sizeof(esp_zb_weekly_schedule_singe_s));
  //   ESP_LOGI(TAG, "Need to write %d bytes for %d", size, (uint8_t)i.first);
  //   auto asd = i.second.data();
  //   char msg[12];
  //   sprintf(msg, "schedule_%x", (uint8_t)i.first);

  //   storage->writeValue<void>(msg, (const void *)asd, size);
  // }
}

const char *getEnumString(DayOfWeekW value) {
  switch (value) {
  case DayOfWeekW::Mon:
    return "Monday";
  case DayOfWeekW::Tue:
    return "Tuesday";
  case DayOfWeekW::Wed:
    return "Wednesday";
  case DayOfWeekW::Thu:
    return "Thrusday";
  case DayOfWeekW::Fri:
    return "Friday";
  case DayOfWeekW::Sat:
    return "Saturday";
  case DayOfWeekW::Sun:
    return "Sunday";
  case DayOfWeekW::Vac:
    return "Vacation or Away";

  default:
    return "";
  }
}

void Heater::printSchedule() {
  time_t now;
  struct tm tm;
  time(&now);
  localtime_r(&now, &tm);

  ESP_LOGI("HEATER", "Time is: %2d.%2d.%4d %2d:%2d:%d", tm.tm_mday, tm.tm_mon,
           tm.tm_year, tm.tm_hour, tm.tm_min, tm.tm_sec);

  for (auto &&c : this->config) {
    ESP_LOGI("HEATER", "%s", getEnumString(c.first));
    for (auto &&i : c.second) {
      auto hour = i.transition_time / 60;
      auto minute = i.transition_time % 60;
      ESP_LOGI("HEATER", "%2d:%2d, Heat: %d", hour, minute, i.tempSetPoint);
    }
  }
}

void Heater::loadLatestZigbeeAttributeValues() {

  // esp_zb_zcl_read_attr_cmd_t read_req;
  // read_req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;

  // read_req.zcl_basic_cmd.dst_addr_u.addr_short = 0x0;
  // read_req.zcl_basic_cmd.dst_endpoint = HA_THERMOSTAT_ENDPOINT;
  // read_req.zcl_basic_cmd.src_endpoint = HA_THERMOSTAT_ENDPOINT;
  // read_req.clusterID = ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT;

  // uint16_t attributes[] = {
  //     ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID,
  //     // ESP_ZB_ZCL_ATTR_THERMOSTAT_UNOCCUPIED_HEATING_SETPOINT_ID
  // };
  // read_req.attr_number = (uint8_t)(sizeof(attributes) /
  // sizeof(*attributes)); read_req.attr_field = attributes;

  // esp_zb_lock_acquire(portMAX_DELAY);
  // esp_zb_zcl_read_attr_cmd_req(&read_req);
  // esp_zb_lock_release();
}

void Heater::reportHeatingMode(bool heating) {
  static uint8_t heat = 0x04;
  static uint8_t off = 0x0;

  ESP_LOGI(TAG, "Setting Mode to: %d", (heating ? heat : off));
  esp_zb_lock_acquire(portMAX_DELAY);
  auto res = esp_zb_zcl_set_attribute_val(
      HA_THERMOSTAT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      ESP_ZB_ZCL_ATTR_THERMOSTAT_RUNNING_MODE_ID, &(heating ? heat : off),
      true);
  esp_zb_lock_release();
  if (res != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGI(TAG, "Attribute Set Result: %x", res);
  }
}

void Heater::insert(std::vector<TimeTempMessage> &cont, TimeTempMessage value) {
  std::vector<TimeTempMessage>::iterator it = std::lower_bound(
      cont.begin(), cont.end(), value,
      [](TimeTempMessage b, TimeTempMessage a) {
        return (a.DayOfWeek == b.DayOfWeek && a.Time > b.Time) ||
               a.DayOfWeek > b.DayOfWeek;
      });                 // find proper position in descending order
  cont.insert(it, value); // insert before iterator it
}

void Heater::updateSystemMode(uint8_t newMode) {
  this->thermostat_cluster.system_mode = newMode;
  storage->writeValue("heat_mode", this->thermostat_cluster.system_mode);
  this->runHeatCheck();
}
void Heater::updateManualTemp(int16_t newTarget) {
  this->manualTemp = newTarget;
  gettimeofday(&(this->manualModeRecv), NULL);

  storage->writeValue("heater_mnlTemp", this->manualTemp);
  storage->writeValue<uint32_t>("heater_mnlTime", this->manualModeRecv.tv_sec);
  this->runHeatCheck();
}
void Heater::updateRemoteTemp(int16_t newTemp) {
  this->remoteTemp = newTemp;
  gettimeofday(&(this->remoteRecv), NULL);
  storage->writeValue("heater_rmtTemp", this->remoteTemp);
  storage->writeValue<uint32_t>("heater_rmtTime", this->remoteRecv.tv_sec);
  this->runHeatCheck();
}
void Heater::updateRuntime(uint32_t newRuntime) {
  this->runtime_in_seconds = newRuntime;

  storage->writeValue("heat_runtime", this->runtime_in_seconds);
}

static void startHeating(Storage *storage, timeval &tv) {
  gpio_set_level(HEATER_GPIO_PIN, 1);
  storage->writeValue("heat_start", tv.tv_sec);
}

static void stopHeating(Storage *storage, Heater *heater, timeval &tv) {

  gpio_set_level(HEATER_GPIO_PIN, 0);
  int64_t heatStartSeconds;
  auto completeRuntime = heater->runtime_in_seconds;
  storage->readValue("heat_start", &heatStartSeconds);
  auto heatPeriod = tv.tv_sec - heatStartSeconds;
  completeRuntime += (uint32_t)heatPeriod;
  storage->writeValue("heat_runtime", completeRuntime);
  ESP_LOGI(TAG, "Heated for %llds, accumulated %lds", heatPeriod,
           completeRuntime);

  esp_zb_lock_acquire(portMAX_DELAY);
  auto res = esp_zb_zcl_set_attribute_val(
      HA_THERMOSTAT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_CUSTOM,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_CUSTOM_RUNTIME_SECONDS_ID,
      &completeRuntime, false);
  esp_zb_lock_release();
  heater->runtime_in_seconds = completeRuntime;

  if (res != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGI(TAG, "Attribute HeatStop Result: %x", res);
  }
}

static void changeTempSource(uint8_t newSource) {

  esp_zb_lock_acquire(portMAX_DELAY);
  auto res = esp_zb_zcl_set_attribute_val(
      HA_THERMOSTAT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_CUSTOM,
      ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
      ESP_ZB_ZCL_ATTR_CUSTOM_TEMPERATURE_SOURCE_ID, &newSource, false);
  esp_zb_lock_release();
  if (res != ESP_ZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGI(TAG, "Attribute Set Result: %x", res);
  }
}

void Heater::runHeatCheck() {
  clock->getCurrentTime(currentTime);
  gettimeofday(&tv, NULL);
  schedules.clear();

  switch (this->thermostat_cluster.system_mode) {
  case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_HEAT:
  case ESP_ZB_ZCL_THERMOSTAT_SYSTEM_MODE_AUTO:
    enableHeatCheck = true;
    break;
  default:
    enableHeatCheck = false;
    break;
  }

  auto temp = this->localSensorTemp;
  if (this->remoteTemp > 0 && this->remoteRecv.tv_sec + 3600 > tv.tv_sec) {
    temp = this->remoteTemp;
    if (this->temperatureSource !=
        ESP_ZB_ZCL_CUSTOM_TEMPERATURE_SOURCE_REMOTE) {
      changeTempSource(ESP_ZB_ZCL_CUSTOM_TEMPERATURE_SOURCE_REMOTE);
    }

    ESP_LOGI(TAG, "Using external sensor temp: %d", this->remoteTemp);
  } else {
    if (tempSensor->tempSensorFound)
      changeTempSource(ESP_ZB_ZCL_CUSTOM_TEMPERATURE_SOURCE_LOCAL);
    else
      changeTempSource(ESP_ZB_ZCL_CUSTOM_TEMPERATURE_SOURCE_NONE);
  }

  if (temp < 5) {

    ESP_LOGI(TAG, "Temp is outside of the allowed range %d", temp);
    return;
  }

  ESP_LOGI(TAG, "Heat check is %s", enableHeatCheck ? "enabled" : "disabled");

  for (auto &&c : this->config) {
    for (auto &&i : c.second) {
      this->insert(schedules, {.DayOfWeek = c.first,
                               .Time = i.transition_time,
                               .Temp = i.tempSetPoint});
    }
  }
  // ESP_LOGI(TAG, "Manuel Temp seconds %llds with offest %lds",
  //          this->manualModeRecv.tv_sec, clock->timeZoneOffsetInSeconds);
  time_t ms = this->manualModeRecv.tv_sec + clock->timeZoneOffsetInSeconds;
  tm manualRec = *localtime(&ms);
  // ESP_LOGI(TAG, "Got manual temp %d for %2d.%2d.%4d %2d:%2d:%d",
  //          this->manualTemp, manualRec.tm_mday, manualRec.tm_mon + 1,
  //          manualRec.tm_year + 1900, manualRec.tm_hour, manualRec.tm_min,
  //          manualRec.tm_sec);

  // Only take manual times from within a week
  if (tv.tv_sec - this->manualModeRecv.tv_sec < 86400 * 7 &&
      this->manualTemp > 0) {

    DayOfWeekW dayOfWeek = (DayOfWeekW)manualRec.tm_wday;

    manualMsg = {.DayOfWeek = dayOfWeek,
                 .Time = (uint16_t)(manualRec.tm_hour * 60 + manualRec.tm_min),
                 .Temp = this->manualTemp};
    this->insert(schedules, manualMsg);
  }
  if (schedules.size() == 0) {

    if (isHeating) {
      this->reportHeatingMode(false);
      isHeating = false;
      stopHeating(storage, this, tv);
    }

  } else {
    auto minutes = (currentTime.tm_hour * 60 + currentTime.tm_min) +
                   clock->timeZoneOffsetInSeconds / 60;
    auto wday = currentTime.tm_wday;

    if (minutes < 0) {
      minutes += 1440;
      wday = (wday - 1 + 7) % 7;
    } else if (minutes >= 1440) {
      minutes -= 1440;
      wday = (wday + 1) % 7;
    }

    auto res = std::find_if(schedules.rbegin(), schedules.rend(),
                            [minutes, wday](TimeTempMessage ttm) {
                              return (wday == (int)ttm.DayOfWeek &&
                                      minutes >= ttm.Time) ||
                                     wday > (int)ttm.DayOfWeek;
                            });
    auto ttm = (res == schedules.rend()) ? schedules.back() : *res;

    auto compressed = ttm.Temp << 16 | ttm.Time;
    if (compressed != this->currentTarget) {

      ESP_LOGI(TAG, "Found new schedule: Day:%s, Time: %2d:%2d, Temp:%f",
               getEnumString(ttm.DayOfWeek), ttm.Time / 60, ttm.Time % 60,
               ZigbeeDevice::s16ToTemperature(ttm.Temp));
      esp_zb_lock_acquire(portMAX_DELAY);
      esp_zb_zcl_set_attribute_val(
          HA_THERMOSTAT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_CUSTOM,
          ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
          ESP_ZB_ZCL_ATTR_CUSTOM_CURRENT_SCHEDULE_ID, &(compressed), false);
      esp_zb_lock_release();
      this->currentTarget = compressed;
    }

    if (manualRec.tm_year > 0 && ttm != manualMsg) {
      // Remove the manual mode from future checks
      manualRec.tm_year = 0;
    }
    auto previous = this->setpointChangeSource;
    if (ttm == manualMsg && this->setpointChangeSource != 0) {
      this->setpointChangeSource = 0x0;
    } else if (ttm != manualMsg) {
      this->setpointChangeSource = 0x1;
    }
    if (previous != this->setpointChangeSource) {
      esp_zb_lock_acquire(portMAX_DELAY);
      esp_zb_zcl_set_attribute_val(
          HA_THERMOSTAT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT,
          ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
          ESP_ZB_ZCL_ATTR_THERMOSTAT_SETPOINT_CHANGE_SOURCE_ID,
          &(this->setpointChangeSource), false);
      esp_zb_lock_release();
    }

    auto shouldHeat = enableHeatCheck && ttm.Temp > temp;
    ESP_LOGI(TAG, "Should heat: %d > %d = %s", ttm.Temp, temp,
             shouldHeat ? "Y" : "N");
    if (isHeating != shouldHeat) {
      isHeating = shouldHeat;
      this->reportHeatingMode(shouldHeat);
      if (shouldHeat) {
        startHeating(storage, tv);
      } else {
        stopHeating(storage, this, tv);
      }
    }
  }
}

void Heater::checkScheduleTask(void *pvParameters) {

  vTaskDelay((10 * 1000) / portTICK_PERIOD_MS);
  auto _this = Heater::GetInstance();
  tm currentTime;
  auto clock = Clock::GetInstance();
  for (;;) {
    clock->getCurrentTime(currentTime);
    // Check every 10 seconds, if time was synced from root
    if (currentTime.tm_year + 1900 < 2000) {

      vTaskDelay((10000 / portTICK_PERIOD_MS));
      continue;
    }
    break;
  }

  for (;;) {
    _this->runHeatCheck();
    clock->getCurrentTime(currentTime);

    // vTaskDelay((10000 / portTICK_PERIOD_MS));
    vTaskDelay(((60 - currentTime.tm_sec) * 1000) /
               portTICK_PERIOD_MS); // Wait till next minute, one second afeter
  }
}

Heater *Heater::_instance = nullptr;

Heater *Heater::GetInstance() {
  if (_instance == nullptr) {
    _instance = new Heater();
  }
  return _instance;
}